public class Test {
	public static void main(String[] args) {
		DagGraph graph = new DagGraph();
		graph.insert("a", "b", '+', "c");
		graph.insert("b", "a", '+', "d");
		graph.insert("c", "b", '+', "c");
		graph.insert("d", "a", '+', "d");
		graph.print();
	}
}