#!/usr/local/bin/perl -w
use strict;
use warnings;

# file I/O variables
my $output_filename = "test.plot";
my $output_image_name = "plot1.png";
my $output_datafile_name = "plot1.dat";
my $input_filename = "combined_results.txt";
my %data;                                                 # data parsed

# plot variables
my $plot_title = "Plot 1";
my $plot_x_axis_name = "x-axis";
my $plot_y_axis_name = "y-axis";




my ($histBits, $btbSize);     # for future dynamic analysis
my $plotData;                 # data to be plotted
my $branchTag = "Branch";
my $cyclesTag = "Cycles";

open (my $IN, '<', $input_filename) || die "Couldn't open output file: $!";
while (my $line = <$IN>) {
     # parse out number of branch stall cycles for given benchmark and config
     if ($line =~ /(\w+).dlx\s+(\w+)\s+branch:\s+(\d+)/){
          $data{$1}{$2}{$branchTag} = $3;
     }
     # parse out number of total cycles for given benchmark and config
     if ($line =~ /(\w+).dlx\s+(\w+)\s+Total cycles:\s+(\d+)/) {
          $data{$1}{$2}{$cyclesTag} = $3;
     }
}
close $IN;

foreach my $benchmark (keys %data) {
     foreach my $parameter (keys %{$data{$benchmark}}){
          # print "$benchmark.$parameter.$branchTag=$data{$benchmark}{$parameter}{$branchTag}\n";
          # print "$benchmark.$parameter.$cyclesTag=$data{$benchmark}{$parameter}{$cyclesTag}\n";
     }
}

open (my $OUT, '>', $output_datafile_name) || die "Couldn't open output file: $!";
print $OUT "Hello world!\n";
close $OUT;


# Plot with gnuplot
# pipe commands into gnuplot
# e.g.: echo "plot 'force.dat' using 1:2 title 'Column' with lines" | gnuplot -p

open (my $PLOT, "| gnuplot") || die "Couldn't open pipe: $!";
print $PLOT "
     set title \"$plot_title\"
     set xlabel \"$plot_x_axis_name\"
     set ylabel \"$plot_y_axis_name\"
     set terminal png enhanced
     set output '$output_image_name'

     plot \"force.dat\" using 1:2 title 'Column' with lines, \\
          \"force.dat\" u 1:3 t 'Beam' with lines
";
close $PLOT;

print "Done\n";

#    To investigate:
#
#    effects of history bits and btb size on dynamic prediction
#         * which scheme gives best results while balancing cost
#         *
#    effectiveness of various schemes on each benchmark
#         * plot each benchmark with several params
#         *
