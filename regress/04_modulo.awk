BEGIN { m = 0 }
{ m++ ; m = m % 2; if (m > 0) print($0) }
