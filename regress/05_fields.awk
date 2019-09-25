BEGIN { line = 0 }
line % 2 == 0 { line++; print($5, $4, $2, $3) }
line % 2 == 1 { line++; print($0) }
