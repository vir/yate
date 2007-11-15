BEGIN {
    for (i=0;i<=255;i++)
	printf("%c",i) >"08b.raw";

    for (j=0;j<=255;j++)
	for (i=0;i<=255;i++)
	    printf("%c%c",i,j) >"16b.raw";

    exit;
}
