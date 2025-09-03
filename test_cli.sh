#!/bin/bash
# Test script for maph CLI tool

echo "=== Testing maph CLI tool ==="
echo

echo "1. Simple key-value mapping:"
echo -e "alice,100\nbob,200\ncharlie,300" | ./maph -b 16 -v
echo

echo "2. Multi-dimensional function (x,y) -> z:"
cat <<EOF | ./maph --input-cols 0,1 --output-cols 2 -b 32 -v
0,0,0
0,1,1
1,0,1
1,1,0
EOF
echo

echo "3. Save and load filter:"
echo -e "red,#FF0000\ngreen,#00FF00\nblue,#0000FF" | ./maph --save colors.maph -b 16
echo "Filter saved. Now querying..."
./maph --load colors.maph --query "red"
echo

echo "4. JSON format:"
cat <<EOF | ./maph --format json -b 32
[
  {"input": "user1", "output": "admin"},
  {"input": "user2", "output": "guest"},
  {"input": "user3", "output": "moderator"}
]
EOF
echo

echo "5. Tuple input/output (x,y,z) -> (a,b):"
cat <<EOF | ./maph --input-cols 0,1,2 --output-cols 3,4 -b 64 -v
1,2,3,10,20
4,5,6,30,40
7,8,9,50,60
EOF
echo

echo "6. With target FPR:"
echo -e "item1\nitem2\nitem3\nitem4\nitem5" | ./maph --fpr 0.01 -b 8 -v
echo

echo "=== All tests completed ==="