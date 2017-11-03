# hardware-branch-prediction
Hardware Simulator and Automation Scripts

The purpose of this simulator and supporting scripts is to caracterize
design tradeoffs for hardware branch prediction. All of the c code is
compiled into a unit that emulates a processor that can run .dlx binaries.
Different options can then be compiled into the hardware, such as static
or dynamic branch prediction. These parameters are spcified in
generate_data.sh, which then executes the binaries with all of the specified
parameters. parse.pl then parses this output, generating more uniform .dat
files which can then be passed into gnuplot to visualize the effects of the
various design decisions. In addition to generating the .dat files, parse.pl
will also run the data through gnuplot and generate graphs as .png files in
the current workspace.
