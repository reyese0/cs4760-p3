# CS4760-project3
<br>Name: Elisa Reyes
<br>Date: 05/15
<br>Environment: vi, visual studio code
<br>How to compile the project: Type 'make'
<br>Type 'make clean' to do a clean
<br> Example of how to run the project: ./oss -n 3 -s 2 -t 4 -i 0.6 -f log.txt
<br>GitHub: https://github.com/reyese0/cs4760-p3
<br>Makeup project: Not previously turned in 
<br>Generative AI used: chatgpt (codex)
<br>Prompts:
<br>Write a program in c that adds tight coordination between the user and oss so that they will only go through an internal loop and check the clock when oss tells them they should. oss will alternate sending messages to the users that it launches. That is, suppose we had three user processes running. oss would first second a message to p0, then after receiving a message back, would send a message to p1, then after getting a message back, send a message to p2 and so on.
The process table should reuse old entries. If the process occupying entry 0 finishes, then the next time you go to launch a process, it should go in that spot. When oss detects that a process has terminated, it can clear out that entry and set its occupied to 0, indicating that it is now a free slot.
The task of oss is to launch a certain number of worker processes with particular parameters. The -t parameter, for example, if it is called with -t 7, then when calling worker processes, it should call them with a time interval randomly between 1 second and 7 seconds (with nanoseconds also random). The -f parameter is for a log file, where you should write the output of oss (THE WORKER OUTPUT SHOULD NOT GO TO THIS FILE). The output of oss should both go to this log file, as well as the screen. When started, oss will initialize the system clock, initialize and set up a message queue and then go into a loop and start doing a fork() and then an exec() call to launch worker processes. However, it should only do this up to simul number of times. oss should make sure to update the process table with information as it is launching user processes. oss will be going into a loop, incrementing the clock and then constantly checking to see if a child has terminated. (use the following pseudocode)
<br>Summary: The inital generated code seemed to have good functionality, but I needed to fix a few issues with the output/log to show the correct information.