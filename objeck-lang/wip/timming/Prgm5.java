public class Prgm5 {
    public static void main(String[] args) {
	long start = System.currentTimeMillis();
	
	run(1000000);
	  
	System.out.println("---------------------------");
	System.out.println("Time: " + ((float)(System.currentTimeMillis() - start) / 1000.0) + 
			   " second(s).");

    }
    
    static void run(int topCandidate) {
	int candidate = 2;
	while(candidate <= topCandidate) {
	    int trialDivisor = 2;
	    int prime = 1;
	    
	    boolean found = true;
	    while(((trialDivisor * trialDivisor) <= candidate) && found == true) {
		if(candidate % trialDivisor == 0) {
		    prime = 0;
		    found = false;
		}
		else {
		    trialDivisor++;
		}
	    }
	    
	    if(found == true) {
		System.out.println(candidate);
	    }
	    candidate++;
	}
    }
}

