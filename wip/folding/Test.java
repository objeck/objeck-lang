import java.util.*;

public class Test {
	public static void main(String[] args) {
	List<Node> input = new ArrayList<Node>();
		// input
		/*
		
		input.add(new Node(Node.Type.INT_LIT, 3));
		input.add(new Node(Node.Type.INT_LIT, 8));
		input.add(new Node(Node.Type.SUB_INT));
		input.add(new Node(Node.Type.STOR_INT_VAR, 0));
		input.add(new Node(Node.Type.LOAD_INT_VAR, 0));
		input.add(new Node(Node.Type.LOAD_INT_VAR, 0));
		input.add(new Node(Node.Type.ADD_INT));
		input.add(new Node(Node.Type.STOR_INT_VAR, 0));
		input.add(new Node(Node.Type.LOAD_INT_VAR, 0));
		input.add(new Node(Node.Type.SHOW_INT));
		*/
		
		// rhs := (5+7)
		input.add(new Node(Node.Type.INT_LIT, 3));
		input.add(new Node(Node.Type.INT_LIT, 7));
		input.add(new Node(Node.Type.INT_LIT, 5));
		input.add(new Node(Node.Type.ADD_INT));
		input.add(new Node(Node.Type.SUB_INT));
		// lhs := 15+3
		input.add(new Node(Node.Type.INT_LIT, 3));
		input.add(new Node(Node.Type.INT_LIT, 15));
		// a := (15+3)+(3-5+7)
		input.add(new Node(Node.Type.ADD_INT));
		input.add(new Node(Node.Type.SUB_INT));
		// 
		input.add(new Node(Node.Type.LOAD_INT_VAR, 0));
		input.add(new Node(Node.Type.SUB_INT));
		// 
		input.add(new Node(Node.Type.STOR_INT_VAR, 0));
		
		input.add(new Node(Node.Type.LOAD_INT_VAR, 0));
		input.add(new Node(Node.Type.SHOW_INT));
		
		for(int i = 0; i < input.size(); i++) {
			System.out.println(input.get(i));
		}
		System.out.println("----------");
		// folding
		Folder folder = new Folder(input);
		List<Node> output = folder.fold();
		for(int i = 0; i < output.size(); i++) {
			System.out.println(output.get(i));
		}
		System.out.println("----------");
		
		
		Executor executor = new Executor(input);
		executor.execute();
		
		Executor executor2 = new Executor(output);
		executor2.execute();
	}
}