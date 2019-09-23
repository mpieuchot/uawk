BEGIN { line=0 }
{
	line++;
	printf("%02d: %s\n", line, $0)
} 
END { printf("\n...read %d records\n", line); }
