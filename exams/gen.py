import random

lower = 1
upper = 9998
count = 10

for i in range(1, 21):  # exam1 to exam20
    filename = f"exam{i}.txt"

    # Generate 300 unique random numbers
    numbers = random.sample(range(lower, upper + 1), count)

    # Write to file with 4-digit formatting
    with open(filename, "w") as f:
        for num in numbers:
            f.write(f"{num:04d}\n")

    print(f"Generated {filename}")
