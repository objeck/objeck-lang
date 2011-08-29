my $topCandidate = 1000000;
my $candidate = 2;
while($candidate <= $topCandidate) {
	my $trialDivisor = 2;
	my $prime = 1;
	 
	my $found = 1;
	while((($trialDivisor * $trialDivisor) <= $candidate) && $found == 1) {
		if(($candidate % $trialDivisor) == 0) {
			$prime = 0;
			$found = 0; 
		}
		else {
			$trialDivisor++;
		}
	}
	
	if($found == 1) {
		print "$candidate\n";
	}
	$candidate++;
}