{ s += $4 }
END { print("sum is", s, " average is", s/NR) }
