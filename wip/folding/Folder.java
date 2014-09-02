import java.util.*;

/***********************
 * Folder node
 ***********************/
public class Folder {
    List<Node> input = null;
    List<Node> output = new ArrayList<Node>();
    Stack<Node> calculationStack = new Stack<Node>();
	
    public Folder(List<Node> input) {
	this.input = input;
    }
	
    public List<Node> fold() {
	for(int i = 0; i < input.size(); i++) {
	    Node node = input.get(i);
	    switch(node.getType()) {
	    case INT_LIT:
		calculationStack.push(node);
		break;

	    case ADD_INT:
	    case SUB_INT:
		calculateFold(node);
		break;	
				
	    default:
		while(!calculationStack.empty()) {
		    output.add(calculationStack.pop());
		}
		output.add(node);
		break;
	    }
	}
		
	return output;
    }
	
    void calculateFold(Node node) {
	if(calculationStack.size() == 1) {
	    output.add(calculationStack.pop());
	}
	else if(calculationStack.size() > 1) {
	    Node left = calculationStack.pop();
	    Node right = calculationStack.pop();
			
	    switch(node.getType()) {
	    case ADD_INT:
		calculationStack.push(new Node(Node.Type.INT_LIT, left.getValue() + 
					       right.getValue()));			
		break;
				
	    case SUB_INT:
		calculationStack.push(new Node(Node.Type.INT_LIT, left.getValue() - 
					       right.getValue()));
		break;
	    }
	}
	else {
	    output.add(node);
	}
    }
}