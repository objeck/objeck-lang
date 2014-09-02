/***********************
 * Parse node
 ***********************/
public class Node {
	Type type;
	int value;

	public Node(Type type) {
		this(type, -1);
	}

	public Node(Type type, int value) {
		this.type = type;
		this.value = value;
	}

	public Type getType() {
		return type;
	}
	
	public int getValue() {
		return value;
	}
	
	public String toString() {
		StringBuilder builder = new StringBuilder();
		switch(type) {
		case INT_LIT:
			builder.append(value);
			break;

		case ADD_INT:
			builder.append("'+'");
			break;
		
		case SUB_INT:
			builder.append("'-'");
			break;
			
		case LOAD_INT_VAR:
			builder.append("load: ");
			builder.append(value);
			break;
			
		case STOR_INT_VAR:
			builder.append("stor: ");
			builder.append(value);
			break;
			
		case SHOW_INT :
			builder.append("show");
			break;
		}
		
		return builder.toString();
	}


	/***********************
	 * Enum class
	 ***********************/
	public enum Type {
		INT_LIT,
		LOAD_INT_VAR,
		STOR_INT_VAR,
		ADD_INT,
		SUB_INT,
		SHOW_INT
	}
}