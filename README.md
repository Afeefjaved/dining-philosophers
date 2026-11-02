# Dining Philosophers – Project 2

TEAM 22 

Name: Afeef Javed 
NET ID: AXJ230065

Name: Dinesh Janapati  
NET ID: dxj220031

## About

This C++17 program runs two solutions:

- Coarse: One Tournament-Tree lock allows one philosopher to eat at a time.
- Fine: Each chopstick has a Peterson lock. Philosophers who do not share chopsticks can eat together.

Each philosopher thinks and eats five times, with random delays of 1–500 milliseconds.

## Compile and Run

```bash
make
./my-dining 5
```

Replace `5` with any integer of at least 2.

To remove the compiled program:

```bash
make clean
```

## Deadlock Prevention

Each philosopher picks up the lower-numbered chopstick first. This prevents circular waiting.

## Notes

The program requires C++17 and thread support. Large inputs depend on available memory and system thread limits. Progress assumes each thread gets CPU time. Invalid inputs produce an error message.