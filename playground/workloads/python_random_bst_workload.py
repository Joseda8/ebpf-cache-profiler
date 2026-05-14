#!/usr/bin/env python3

import random
import sys


class Node:
    def __init__(self, value):
        self.value = value
        self.left = None
        self.right = None


def insert(root, value):
    if root is None:
        return Node(value)

    if value < root.value:
        root.left = insert(root.left, value)
    else:
        root.right = insert(root.right, value)

    return root


def generate_random_list(n, seed=42):
    random.seed(seed)
    return [random.uniform(0, n) for _ in range(n)]


def build_tree(numbers):
    root = None
    for number in numbers:
        root = insert(root, number)
    return root


def main() -> int:
    workload_size = 5000000
    if len(sys.argv) == 2:
        try:
            workload_size = int(sys.argv[1])
        except ValueError:
            print("Invalid workload size. Expected a positive integer.", file=sys.stderr)
            return 1
    elif len(sys.argv) != 1:
        print("Usage: python_random_bst_workload.py [node_count]", file=sys.stderr)
        return 1

    if workload_size <= 0:
        print("node_count must be positive.", file=sys.stderr)
        return 1

    numbers = generate_random_list(workload_size)
    tree = build_tree(numbers)
    if tree is None:
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
