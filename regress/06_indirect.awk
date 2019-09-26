BEGIN { var = 1 }
$var == "/*" { $0 = "" }
$var == "*/" { $0 = "" }
$var == "*"  { $var = "//" }
{ print($0) }
