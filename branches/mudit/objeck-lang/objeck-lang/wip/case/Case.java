import java.util.*;

public class Case {	
    public static void main(String[] args) {
	// get values
	List<Integer> values = new ArrayList<Integer>();
	for(String arg : args) {
	    values.add(Integer.parseInt(arg));		
	}
	System.out.println("values: " + values);
	// sort
	Collections.sort(values);
	// parse and print
	BinaryArrayTree binaryTree = new BinaryArrayTree(values);
	binaryTree.print();
    }
}

class BinaryArrayTree {
    private List<Integer> values;
    private Node root;
    
    public BinaryArrayTree(List<Integer> values) {
	this.values = values;
	root = divide(0, values.size() - 1);
    }
    
    public Node divide(int start, int end) {
	Node node = null;
	int distance = end - start;
	if(distance == 1) {
	    node = new Node(values.get(end), Operation.LESS,
			    new Node(values.get(start)),
			    new Node(values.get(end)));
	    return node;
	}		
	else if(distance == 2) {
	    node = new Node(values.get(end - 1), Operation.LESS_OR_EQUAL,
			    new Node(values.get(start)),
			    new Node(values.get(end)));
	}		
	else {
	    int length = end - start;
	    int middle = length / 2 + start;
	    System.out.println("??: " + middle + ", " + length);
	    if(length > 1 && length %2 != 0) {
		node = new Node(values.get(middle), Operation.LESS_OR_EQUAL,
				divide(start, middle - 1),
				divide(middle + 1, end));
	    }
	    else if(length > 1) {		
		node = new Node(values.get(end), Operation.LESS,
				divide(start, middle),
				divide(middle + 1, end));
	    }
	}
	
	return node;
    }

    private void print(Node node) {
	if(node != null) {
	    System.out.println(node);	    
	    print(node.getLeft());
	    print(node.getRight());
	}
    }
    
    public void print() {
	print(root);
    }
    
}

class Node {
    static int uniqueId = 0;
    private int id;
    private int value;
    private Operation operation;
    private Node left;
    private Node right;
	
    public Node(Integer value) {
	this.id = uniqueId++;
	this.value = value.intValue();
	this.operation = Operation.EQUAL;
	this.left = null;
	this.right = null;
    }
	
    public Node(Integer value, Operation operation,
		Node left, Node right) {
	this.id = uniqueId++;
	this.operation = operation;
	this.value = value.intValue();
	this.left = left;
	this.right = right;
    }

    public int getId() {
	return id;
    }

    public int getValue() {
	return value;
    }

    public Node getLeft() {
	return left;
    }

    public Node getRight() {
	return right;
    }

    public String toString() {
	StringBuffer buffer = new StringBuffer();

	buffer.append("id=");
	buffer.append(id);
	if(operation == Operation.LESS) {	    
	    buffer.append(": eval[value<");
	    buffer.append(value);
	}
	else if(operation == Operation.EQUAL) {
	    buffer.append(": eval[value=");
	    buffer.append(value);
	    buffer.append("] *found*, false=*not found*");	    
	}
	else {
	    buffer.append(": eval[value=");
	    buffer.append(value);
	    buffer.append("] *found* | value<");
	    buffer.append(value);
	}
	
	if(left != null && right != null) {
	    buffer.append("], true=");
	    buffer.append(left.getId());
	    buffer.append(", false=");
	    buffer.append(right.getId());
	}
	
	return buffer.toString();
    }
}

enum Operation {
    LESS,
    EQUAL,
    LESS_OR_EQUAL
};