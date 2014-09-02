import java.util.*;

public class Select {
	public static void main(String[] args) {
		if(args.length == 0)
			return;
	
		List<Integer> numbers = new ArrayList<Integer>();
		for(String string : args) {
			numbers.add(Integer.parseInt(string));
		}
		
		for(int i : numbers) {
			System.out.print(i);
			System.out.print(',');
		}
		System.out.println();
		System.out.println("----------------------");
		
		Node node;
		if(numbers.size() > 1) {
			Collections.sort(numbers);
			node = divide(numbers, 0, numbers.size() - 1);
		}
		else {
			node = new Node(numbers.get(0), Node.Type.NONE);
		}
		walk(node);		
	}
	
	public static Node divide(List<Integer> numbers, int start, int end) {
		final int size = end - start + 1;		
		if(size < 4) {
			if(size == 2) {
				Node left = new Node(numbers.get(start), Node.Type.NONE);
				Node right = new Node(numbers.get(start + 1), Node.Type.NONE);
				Node node = new Node(numbers.get(start + 1), Node.Type.LESS_THAN);
				node.setLeft(left);
				node.setRight(right);
				
				return node;
			} 
			else {
				Node left = new Node(numbers.get(start), Node.Type.NONE);
				Node right = new Node(numbers.get(start + 2), Node.Type.NONE);
				Node node = new Node(numbers.get(start + 1), numbers.get(start + 2));
				node.setLeft(left);
				node.setRight(right);
				
				return node;
			}	
		}	
		else {
			final int middle = size / 2 + start;
			if(size % 2 == 0) {
				Node left = divide(numbers, start, middle - 1);
				Node right = divide(numbers, middle, end);
				Node node = new Node(numbers.get(middle), Node.Type.LESS_THAN);
				node.setLeft(left);
				node.setRight(right);
				
				return node; 
			}
			else {
				Node left = divide(numbers, start, middle - 1);			
				Node right = divide(numbers, middle + 1, end);
				Node node = new Node(numbers.get(middle), numbers.get(middle));
				node.setLeft(left);
				node.setRight(right);
				
				return node;
			}
		}		
	}
	
	public static void walk(Node node) {
		if(node != null) {
			System.out.println(node);
			walk(node.getLeft());
			walk(node.getRight());
		}
	}	
}

class Node {
	static int uniqueId = 0;
	
	public enum Type {
		NONE,
		LESS_THAN,
		LESS_THAN_OR_EQUAL
	}
	
	int id;
	Type type;
	int value;
	int value2;
	Node left;
	Node right;

	public Node(int value, Type type) {
		this.value = value;	
		this.type = type;
		this.id = uniqueId++;
	}
	
	public Node(int value, int value2) {
		this.value = value;
		this.value2 = value2;		
		this.type = Type.LESS_THAN_OR_EQUAL;
		this.id = uniqueId++;
	}
	
	public int getId() {
		return this.id;
	}
	
	public void setLeft(Node left) {
		this.left = left;
	}
	
	public Node getLeft() {
		return this.left;
	}
	
	public void setRight(Node right) {
		this.right = right;
	}
	
	public Node getRight() {
		return this.right;
	}
	
	public String toString() {
		StringBuilder builder = new StringBuilder();
		
		builder.append(id);
		builder.append(": ");
		switch(type) {
		case NONE:
			builder.append("x=");
			builder.append(value);
			builder.append(", *not found* ");
			break;
			
		case LESS_THAN:
			builder.append("x<");
			builder.append(value);
			builder.append(" [");
			builder.append(left.getId());
			builder.append(",");
			builder.append(right.getId());
			builder.append("]");
			break;
				
		case LESS_THAN_OR_EQUAL:
			builder.append("x=");
			builder.append(value);
			builder.append(", x<");
			builder.append(value2);
			builder.append(" [");
			builder.append(left.getId());
			builder.append(",");
			builder.append(right.getId());
			builder.append("]");
			break;
		}	
		
		return builder.toString();
	}
}
