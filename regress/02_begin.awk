BEGIN { printf("%3s\n", "ciao"); t="bye" }
{ } # nothing
END { print(t); }
