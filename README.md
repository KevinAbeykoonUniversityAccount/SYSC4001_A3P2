## How to compile and run
**To compile you can enter in terminal:** gcc -std=c11 -O2 -Wall -pthread ta_marking.c -o ta_marking  

**To run you can enter in the terminal:**   ./ta_marking <num_TAs> <rubric_file> <exams_dir> (--sync)

- ta_marking is the file name
- <num_TAs> is the number of TAs you want to mark the work
- <rubric_file> is the rubric.txt
- <exams_dir> is the folder that contains the exam txt files you want to run
- --sync can be omitted or included. If you want to use semaphores (part b) include the --sync, otherwise omit it (part a)


## Test Cases  
All the test cases listed below passed successfully. Test folders are listed under the exams directory.

**Test 1**:
This test tests the directory scan: sorting the files alphabetically and loading no duplicates into the shared_t obj. You can print out the files field to test this.

**Test 2**: 
This test tests the special end character 9999. I placed 9999 in the middle of exam3.txt to test if it correctly stops at that line and doesn't go further.

**Test 3**: 
This test tests an empty directory. It should pop up an error message if it cant find files. Github doesn't allow empty directories though.

**Test 4**: 
This test tests the max files count. Since there should be a maximum amount of files as to not overwelm a computer or enter a extremely long loop, I maxed it out at 200 files and placed over 200
files in test4 directory. It should not take more than 200.

**Test 5**: 
By altering the number of TAs you can see how having more TAs working on the marking speeds up the marking.

**Test 6**: 
By running any of the exam directories, carefully trace the log messages to see if they match the expected output. I did this, and it all was correct.



