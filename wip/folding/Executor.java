/***********************
 * Executor node
 ***********************/
import java.util.*;

public class Executor {
	List<Node> input = null;
	int[] memory = new int[32];
	int stackPointer = 0;
	int[] executionStack = new int[32];

	public Executor(List<Node> input) {
		this.input = input;
	}
	
	void pushInt(int value) {
		executionStack[stackPointer++] = value;
	}
	
	int popInt() {
		return executionStack[--stackPointer];
	}
	
	public void execute() {
		
		
		for(int i = 0; i < input.size(); i++) {
			Node node = input.get(i);
			switch(node.getType()) {
			case INT_LIT:
				pushInt(node.getValue());
				break;
				
			case LOAD_INT_VAR:
				pushInt(memory[node.getValue()]);
				break;
				
			case STOR_INT_VAR:
				memory[node.getValue()] = popInt();
				break;
				
			case ADD_INT:
				pushInt(popInt() + popInt());
				break;
				
			case SUB_INT:
				pushInt(popInt() - popInt());
				break;
				
			case SHOW_INT:
				System.out.println(popInt());
				break;
			}
		}
	}
}