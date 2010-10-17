import java.util.*;

public class DagNode {
	private static int count = 0;
	private String key;
	private DagNode left;
	private DagNode right;
	private List<DagNode> children = new ArrayList<DagNode>();
	private int id;
	
	public DagNode(String key) {
		this.key = key;
	}
	
	public DagNode(String key, DagNode left, DagNode right) {
		this.key = key;
		this.left = left;
		this.right = right;
		id = count++;
	}
	
	public String getKey() {
		return this.key;
	}
	
	DagNode getLeft() {
		return this.left;
	}
	
	DagNode getRight() {
		return this.right;
	}
	
	void addChild(DagNode child) {
		children.add(child);
	}
	
	boolean hasChild(DagNode node) {
		for(DagNode child : children) {
			if(node.getKey().equals(child.getKey())) {
				return true;
			}
		}
		
		return false;
	}
	
	public String toString() {
		return getKey();
	}
	
	public void print() {
		if(children.size() == 0) {
			System.out.println(this);
		}
		else {
			System.out.println(children.get(0) + " = " + this);
			for(int i = 1; i < children.size(); i++) {
				System.out.println(children.get(i) + " = " + children.get(0));
			}
		}
	}
}