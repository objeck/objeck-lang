import sys

def make_tree(depth):
    if depth > 0:
        depth -= 1
        return (make_tree(depth), make_tree(depth))
    return (None, None)

def check_tree(node):
    (left, right) = node
    if left is None:
        return 1
    return 1 + check_tree(left) + check_tree(right)

def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 10
    min_depth = 4
    max_depth = max(min_depth + 2, n)
    stretch_depth = max_depth + 1

    print("stretch tree of depth %d\t check: %d" % (stretch_depth, check_tree(make_tree(stretch_depth))))

    long_lived_tree = make_tree(max_depth)

    for depth in range(min_depth, stretch_depth, 2):
        iterations = 2 ** (max_depth - depth + min_depth)
        check = 0
        for i in range(1, iterations + 1):
            check += check_tree(make_tree(depth))
            check += check_tree(make_tree(depth))
        print("%d\t trees of depth %d\t check: %d" % (iterations * 2, depth, check))

    print("long lived tree of depth %d\t check: %d" % (max_depth, check_tree(long_lived_tree)))

main()
