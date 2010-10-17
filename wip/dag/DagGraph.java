import java.util.*;

public class DagGraph {
	List<DagNode> nodes = new ArrayList<DagNode>();
	Map<String, DagNode> lookup = new HashMap<String, DagNode>();
	
	public void insert(String resultValue, String leftValue, char operationValue, String rightValue) {
		final String key = leftValue + operationValue + rightValue;
		DagNode result = new DagNode(resultValue);
		if(lookup.containsKey(key)) {
			DagNode parent = lookup.get(key);
			if(!parent.hasChild(result)) {
				parent.addChild(result);
			}
			else {
				insertNew(result, key, leftValue, rightValue);
			}	
		}
		else {
			insertNew(result, key, leftValue, rightValue);
		}	
	}
	
	private void insertNew(DagNode result, String key, String leftValue, String rightValue) {
		// check left value
		DagNode left;
		if(lookup.containsKey(leftValue)) {
			left = lookup.get(leftValue);
		}
		else {
			left = new DagNode(leftValue);
			lookup.put(left.getKey(), left);
			// nodes.add(left);
		}
		
		// check right value
		DagNode right;
		if(lookup.containsKey(rightValue)) {
			right = lookup.get(rightValue);
		}
		else {
			right = new DagNode(rightValue);
			lookup.put(right.getKey(), right);
			// nodes.add(right);
		}
			
		DagNode parent = new DagNode(key, left, right);
		lookup.put(parent.getKey(), parent);
		parent.addChild(result);
		nodes.add(parent);
	}	
	
	void print() {
		for(DagNode node : nodes) {
			node.print();
		}
	}
}