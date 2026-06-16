// binarytrees — idiomatic single-threaded Java, matched to the Python/Ruby reference
// Usage: java binarytrees <depth>

public class binarytrees {
    static final class TreeNode {
        TreeNode left, right;
        TreeNode(TreeNode left, TreeNode right) { this.left = left; this.right = right; }
    }

    static TreeNode makeTree(int depth) {
        if (depth == 0) return new TreeNode(null, null);
        depth -= 1;
        return new TreeNode(makeTree(depth), makeTree(depth));
    }

    static int checkTree(TreeNode node) {
        if (node.left == null) return 1;
        return 1 + checkTree(node.left) + checkTree(node.right);
    }

    public static void main(String[] args) {
        int n = args.length > 0 ? Integer.parseInt(args[0]) : 10;
        int minDepth = 4;
        int maxDepth = Math.max(minDepth + 2, n);
        int stretchDepth = maxDepth + 1;

        System.out.println("stretch tree of depth " + stretchDepth +
            "\t check: " + checkTree(makeTree(stretchDepth)));

        TreeNode longLivedTree = makeTree(maxDepth);

        for (int depth = minDepth; depth <= maxDepth; depth += 2) {
            int iterations = 1 << (maxDepth - depth + minDepth);
            int check = 0;
            for (int i = 1; i <= iterations; i++) {
                check += checkTree(makeTree(depth));
                check += checkTree(makeTree(depth));
            }
            System.out.println((iterations * 2) + "\t trees of depth " + depth +
                "\t check: " + check);
        }

        System.out.println("long lived tree of depth " + maxDepth +
            "\t check: " + checkTree(longLivedTree));
    }
}
