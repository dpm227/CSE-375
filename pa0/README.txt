**** Change your Compiler on Sunlab

  You will need to use a newer compiler than the default one on the sunlab
  machines. The current compiler is gcc 8.5.0.
  If you use more modern C++ features, you might need a more recent GCC version.
  In order to switch gcc compiler, you have to load one of the modules already installed
  for gcc:
    execute: 'gcc --version' --> you should see "gcc (GCC) 8.5.0 20210514 (Red Hat 8.5.0-16)";
    execute: 'which gcc' --> you should see "/usr/bin/gcc";
    execute: 'module avail' --> you should be able to see different versions of the gcc
    compiler. For example, gcc-11.2.0.

  In order to load that module, you have two options:
    (1) temporary change (must be done every time you start a new terminal window)
	execute: 'module load gcc-11.2.0'
	execute: 'which gcc' --> you should see "/opt/gcc-11.2.0/bin/gcc";
	execute: 'gcc --version' --> you should see "gcc (CSE SunLab GCC) 11.2.0";
    (2) permanent change (new version loaded every time you open a new terminal window)
    	Add "module load gcc-11.2.0" in ~/.profile

**** Code -> Debug -> Code
  The given file contains a broad specification of the problem and how it is expected
  to be coded. You might find directions not so strict. This has been done
  intentionally to give students the freedom to design their own solutions. Don't be
  afraid to explore.

**** Recommendation:
  Please do not overload the sunlab machines with an unreasonably large amount of parallel threads.
  To play with parallelism, you should be satisfied enough to run with 4 to 24 threads. Recall that
  nodes in the sunlab cluster have 16 CPUs (try 'less /proc/cpuinfo'), therefore any configuration
  with a higher number of parallel threads won't produce an actual parallel execution.
  To check the real-time utilization of the machine on sunlab, use the "top" command. To restrict
  the list of processes to those associated with your account only, use "top -u your-sunlab-user".
