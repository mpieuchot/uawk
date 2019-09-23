BEGIN { s = 0 }
s < 10 { s++; print($0) }
s == 10 { exit }
s > 10 { printf("never printed") }
