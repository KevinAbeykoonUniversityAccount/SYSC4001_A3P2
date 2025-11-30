/*
  ./ta_marking <num_TAs> <rubric_file> <exams_dir> (--sync)
   I separated part 2 by having to provide --sync in the command.
   if --sync is provided, semaphores are used. 
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <errno.h>
#include <ctype.h>

#define SHM_NAME "/ta_shared_mem_v1"
#define MAX_FILES 200
#define MAX_PATH 256

typedef struct {
    char rubric[5];  
    // current exam
    char current_student[5]; 
    int question_marked[5];  // 0/1 for five questions

    // file list
    char filenames[MAX_FILES][MAX_PATH];
    int total_files;
    int current_index;
    int terminate;     // set to 1 when student==9999
} shared_t;

// semaphore names 
const char *SEM_RUBRIC = "/sem_rubric_write_only_v1"; // writer for rubric
const char *SEM_QUESTION = "/sem_question_lock_v1";   // protect question_marks
const char *SEM_LOAD = "/sem_load_lock_v1";           // protect loading next exam

// sleep a random duration 
void rand_sleep_ms(unsigned int *seedp, int ms_min, int ms_max) {
    int r = ms_min + (rand_r(seedp) % (ms_max - ms_min + 1));
    usleep(r * 1000);
}

// read rubric file into shared->rubric
int load_rubric_into_shared(shared_t *shared, const char *rubric_path) {
    FILE *f = fopen(rubric_path, "r");

    // error handling
    if (!f) {
        perror("fopen rubric");
        return -1;
    }

    char line[256];
    int i = 0;

    while (i < 5 && fgets(line, sizeof line, f)) {
        // find comma
        char *comma = strchr(line, ',');
        if (!comma) {
            // error found
            fprintf(stderr, "Error! Cannot parse line.\n", i + 1);
            fclose(f);
            return -1;
        } 

        // skip space
        char *p = comma + 1;
        while (*p == ' ') p++;
        shared->rubric[i] = (p[0] == '\0' || p[0] == '\n') ? ('A' + i) : p[0];
        
        i++;
    }

    if (i < 5) {
        fprintf(stderr, "Error! Cannot find 5 lines.\n");
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

// write current shared rubric back to rubric file
int write_rubric_to_file(shared_t *shared, const char *rubric_path) {
    FILE *f = fopen(rubric_path, "w");
    if (!f) {
        perror("fopen rubric write");
        return -1;
    }
    for (int i = 0; i < 5; ++i) {
        fprintf(f, "%d, %c\n", i+1, shared->rubric[i]);
    }
    fclose(f);
    return 0;
}

// load the exam into shared memory
int load_current_exam_to_shared(shared_t *shared) {
    if (shared->current_index < 0 || shared->current_index >= shared->total_files) return -1; // Error handling

    char path[MAX_PATH];
    strncpy(path, shared->filenames[shared->current_index], sizeof path);
    FILE *f = fopen(path, "r");

    if (!f) {
        // attempt to open relative or absolute
        perror("fopen exam file");
        return -1;
    }
    char buf[256];
    // find first 4 digit string the file
    int found = 0;
    while (fgets(buf, sizeof buf, f)) {
        // search for a 4 digit number 
        for (int i = 0; buf[i] != '\0'; ++i) {
            if (isdigit((unsigned char)buf[i]) &&
                isdigit((unsigned char)buf[i+1]) &&
                isdigit((unsigned char)buf[i+2]) &&
                isdigit((unsigned char)buf[i+3])) {

                char tmp[5]; 
                tmp[0] = buf[i]; 
                tmp[1] = buf[i+1]; 
                tmp[2] = buf[i+2]; 
                tmp[3] = buf[i+3];
                tmp[4] = '\0';
                strncpy(shared->current_student, tmp, sizeof(shared->current_student));
                found = 1;
                break;
            }
        }
        if (found) break;
    }

    fclose(f);


    if (!found) { // default to "0000" when not found. this is error handling
        strncpy(shared->current_student, "0000", sizeof shared->current_student);
    }

    // reset marks
    for (int i = 0; i < 5; ++i) shared->question_marked[i] = 0;
    return 0;
}

// scan directory, fill shared->filenames array (absolute paths), sort alphabetically
int scan_exam_directory(const char *dirpath, shared_t *shared) {
    DIR *d = opendir(dirpath);
    if (!d) {
        perror("opendir exams dir"); // error handling
        return -1;
    }

    struct dirent *ent;
    char tmp[MAX_PATH];
    int count = 0;

    // collect all filenames in the exams folder
    char *list[MAX_FILES];
    for (int i = 0; i < MAX_FILES; ++i) list[i] = NULL;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_type == DT_REG || ent->d_type == DT_UNKNOWN) {
            // build full path
            snprintf(tmp, sizeof tmp, "%s/%s", dirpath, ent->d_name);
            if (count < MAX_FILES) list[count] = strdup(tmp);
            count++;
        }
    }
    closedir(d);

    // sort the filenames so I can read the output logs can follow it easily
    int use = (count < MAX_FILES) ? count : MAX_FILES;
    qsort(list, use, sizeof(char *), (int(*)(const void*, const void*)) strcmp);

    // copy to shared memory
    for (int i = 0; i < use; ++i) {
        strncpy(shared->filenames[i], list[i], MAX_PATH);
        free(list[i]);
    }
    shared->total_files = use;
    return 0;
}

int main(int argc, char **argv) {
    // checking arguments
    if (argc < 4) {
        fprintf(stderr, "Inccorect args: %s <num_TAs> <rubric_file> <exams_dir> [--sync]\n", argv[0]);
        exit(1);
    }
    int nTAs = atoi(argv[1]);
    if (nTAs < 2) {
        fprintf(stderr, "Please provide n >= 2 TAs\n");
        exit(1);
    }
    const char *rubric_path = argv[2];
    const char *exams_dir = argv[3];
    int use_sync = 0;
    if (argc >= 5 && strcmp(argv[4], "--sync") == 0) use_sync = 1; // for part b

    // create shared memory
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) { perror("shm_open"); exit(1); }
    size_t shm_size = sizeof(shared_t); //
    if (ftruncate(shm_fd, shm_size) == -1) { perror("ftruncate"); exit(1); }
    shared_t *shared = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared == MAP_FAILED) { perror("mmap"); exit(1); }

    // initialize shared memory region
    memset(shared, 0, sizeof(shared_t));
    shared->current_index = 0;
    shared->terminate = 0;

    // scan exam directory and store filenames in shared memory
    if (scan_exam_directory(exams_dir, shared) != 0) {
        fprintf(stderr, "error scanning exams directory\n");
        munmap(shared, shm_size);
        shm_unlink(SHM_NAME);
        exit(1);
    }

    // load rubric into shared memory
    if (load_rubric_into_shared(shared, rubric_path) != 0) {
        fprintf(stderr, "error loading rubric\n");
        munmap(shared, shm_size);
        shm_unlink(SHM_NAME);
        exit(1);
    }

    // load first exam into shared memory
    if (load_current_exam_to_shared(shared) != 0) {
        fprintf(stderr, "Error loading first exam\n");
    }

    // create semaphores if requested by --sync
    sem_t *sem_rubric = NULL;
    sem_t *sem_question = NULL;
    sem_t *sem_load = NULL;
    if (use_sync) {
        // unlink first
        sem_unlink(SEM_RUBRIC);
        sem_unlink(SEM_QUESTION);
        sem_unlink(SEM_LOAD);
        sem_rubric = sem_open(SEM_RUBRIC, O_CREAT | O_EXCL, 0666, 1);
        sem_question = sem_open(SEM_QUESTION, O_CREAT | O_EXCL, 0666, 1);
        sem_load = sem_open(SEM_LOAD, O_CREAT | O_EXCL, 0666, 1);

        if (sem_rubric == SEM_FAILED || sem_question == SEM_FAILED || sem_load == SEM_FAILED) {
            perror("sem_open");
            use_sync = 0;
            sem_rubric = sem_question = sem_load = NULL;
        }
    }

    printf("Parent: starting with %d TAs. total exams found = %d. sync=%d\n",
           nTAs, shared->total_files, use_sync);

    // fork n TAs children
    for (int i = 0; i < nTAs; ++i) {
        pid_t p = fork();
        if (p < 0) { perror("fork"); exit(1); }
        if (p == 0) {
            // random seed
            unsigned int seed = (unsigned int)(time(NULL) ^ getpid() ^ (i<<8));
            int myid = i + 1;
            printf("TA %d (pid %d): started\n", myid, getpid());

            while (1) {
                // check termination flag
                if (shared->terminate) {
                    printf("TA %d: noticed terminate flag. exiting.\n", myid);
                    _exit(0);
                }

                // Read rubric (all TAs can read at same time)
                // randomly decide whether to correct it
                for (int q = 0; q < 5; ++q) {

                    // decision takes between 0.5 - 1 second
                    rand_sleep_ms(&seed, 500, 1000);
                    int decide = rand_r(&seed) % 2; // 0 or 1
                    if (decide) { // the ta will correct it, make sure only one ta attempts to
                        if (use_sync && sem_rubric) sem_wait(sem_rubric);
                        // make the character the next ASCII
                        char oldc = shared->rubric[q];
                        char newc = oldc + 1;
                        if (newc > 'Z') newc = 'A';
                        shared->rubric[q] = newc;

                        // save to file
                        if (use_sync) {
                            // hold sem_rubric
                            if (write_rubric_to_file(shared, rubric_path) != 0) {
                                fprintf(stderr, "TA %d: failed to write rubric\n", myid);
                            } else {
                                printf("TA %d: corrected rubric line %d: %c -> %c (saved)\n",
                                       myid, q+1, oldc, newc);
                            }

                        } else {
                            // no semaphores case
                            if (write_rubric_to_file(shared, rubric_path) != 0) {
                                fprintf(stderr, "TA %d: failed to write rubric (no sync)\n", myid);
                            } else {
                                printf("TA %d: (no sync) corrected rubric line %d: %c -> %c (saved)\n",
                                       myid, q+1, oldc, newc);
                            }
                        }

                        if (use_sync && sem_rubric) sem_post(sem_rubric);

                    } 
                }

                // pick an unmarked question
                int picked = -1;
                if (use_sync && sem_question) sem_wait(sem_question);
                for (int q = 0; q < 5; ++q) {
                    if (shared->question_marked[q] == 0) {
                        // claim th e question
                        shared->question_marked[q] = 1;
                        picked = q;
                        break;
                    }
                }

                if (use_sync && sem_question) sem_post(sem_question);

                if (picked == -1) {
                    // this means all question shave been marked    
                    int loaded = 0;

                    // for sync
                    if (use_sync && sem_load) {
                        sem_wait(sem_load);
                        // check again
                        int all_marked = 1;
                        for (int q=0;q<5;q++) if (shared->question_marked[q]==0) { all_marked = 0; break; }

                        if (all_marked) {
                            // advance index
                            shared->current_index++;
                            if (shared->current_index >= shared->total_files) {
                                // no more exams: set terminate
                                shared->terminate = 1;
                                printf("TA %d: no more exams. setting terminate.\n", myid);
                            } else {
                                if (load_current_exam_to_shared(shared) == 0) {
                                    printf("TA %d: loaded next exam: %s (student %s)\n",
                                           myid, shared->filenames[shared->current_index], shared->current_student);
                                } else {
                                    fprintf(stderr, "TA %d: failed loading next exam\n", myid);
                                }
                            }
                            loaded = 1;
                        }
                        sem_post(sem_load);

                    } else {
                        // no sync
                        int all_marked = 1;
                        for (int q=0;q<5;q++) if (shared->question_marked[q]==0) { all_marked = 0; break; }
                        if (all_marked) {
                            shared->current_index++;
                            if (shared->current_index >= shared->total_files) {
                                shared->terminate = 1;
                                printf("TA %d (no-sync): no more exams. set terminate.\n", myid);
                            } else {
                                if (load_current_exam_to_shared(shared) == 0) {
                                    printf("TA %d (no-sync): loaded next exam: %s (student %s)\n",
                                           myid, shared->filenames[shared->current_index], shared->current_student);
                                }
                            }
                            loaded = 1;
                        }
                    }

                    // check for termination
                    if (shared->terminate) {
                        printf("TA %d: terminate requested. exiting.\n", myid);
                        _exit(0);
                    }
                    // if we loaded a new exam, check if it's the 9999 
                    if (loaded) {
                        if (strcmp(shared->current_student, "9999") == 0) {
                            shared->terminate = 1;
                            printf("TA %d: reached student 9999, terminate (pid %d)\n", myid, getpid());
                            _exit(0);
                        }
                    }

                    rand_sleep_ms(&seed, 100, 300);
                    continue;

                } else { // already picked a question

                    printf("TA %d: marking student %s question %d (pid %d)\n",
                           myid, shared->current_student, picked+1, getpid());
                    // marking takes 1 - 2 seconds
                    rand_sleep_ms(&seed, 1000, 2000);
                    printf("TA %d: finished marking student %s question %d\n",
                           myid, shared->current_student, picked+1);
                    // loop to try get another question
                }
            } 
            _exit(0);
        } // terminate child
    } 

    // parent waits for termination flag
    while (1) {
        sleep(1);
        if (shared->terminate) {
            printf("Parent: terminate flag detected. Killing children and exiting.\n");
            break;
        }
    }

    // "kill children" by waiting
    while (1) {
        pid_t w = wait(NULL);
        if (w <= 0) break;
    }

    // cleanup semaphores
    if (use_sync) {
        if (sem_rubric) { sem_close(sem_rubric); sem_unlink(SEM_RUBRIC); }
        if (sem_question) { sem_close(sem_question); sem_unlink(SEM_QUESTION); }
        if (sem_load) { sem_close(sem_load); sem_unlink(SEM_LOAD); }
    }

    // unmap and unlink shared memory
    munmap(shared, shm_size);
    shm_unlink(SHM_NAME);

    printf("Parent: exiting.\n");
    return 0;
}
