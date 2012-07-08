# The Computer Language Benchmarks Game
# http://shootout.alioth.debian.org/
#
# contributed by Rodrigo de Almeida Siqueira

use strict;

my $MAXITER = 50;
my $LIMIT = 4;
my $xmin = -1.5;
my $ymin = -1;

my ($w, $h);

$w = $h = shift || 200;
my $invN = 2/$w;

print "P4\n$w $h\n"; # PBM image header

my $checknext=1;

for my $y (0..$h-1) {

  my @v;

  my $Ci = $y * $invN + $ymin;

  X:
  for my $x (0..$w-1) {

    my ($Zr, $Zi, $Tr, $Ti);

    my $Cr = $x * $invN + $xmin;

    if ($checknext) {

        # Iterate with checking (likely outer pixel)
        for (1..$MAXITER) {
          $Zi = 2 * $Zr * $Zi + $Ci;
          $Zr = $Tr - $Ti + $Cr;
          $Ti = $Zi * $Zi;
          $Tr = $Zr * $Zr;

          if ($Tr + $Ti > $LIMIT) {
            push @v, 0; # White pixel
            next X;
          }
        }

        push @v, 1;     # Black pixel
        $checknext = 0; # Is inner pixel.

    } else {

      # Iterate without checking (likely inner pixel)

      for (1..$MAXITER) {
        $Zi = 2 * $Zr * $Zi + $Ci;
        $Zr = $Tr - $Ti + $Cr;
        $Ti = $Zi * $Zi;
        $Tr = $Zr * $Zr;
      }

      if ($Tr+$Ti <= 4) {
        push @v, 1;
      } else { # $Tr+$Ti is a large number or overflow ('nan' or 'inf')
        push @v, 0;
        $checknext = 1;
      }

    }

  }

  print pack 'B*', pack 'C*', @v;

}

