#include <stdio.h>

int main(int argc, const char **argv)
{
    int n = 0;
    if (argv[1][0] == 'b') {
	unsigned char c;
	printf("static unsigned char %s[] = {",argv[2]);
	while (fread(&c,1,1,stdin)) {
	    if (n)
		printf(",");
	    if (((n++) % 16) == 0)
		printf("\n");
	    printf(" 0x%02X",c);
	}
	printf("\n};\n");
    }
    else {
	unsigned short s;
	printf("static unsigned short %s[] = {",argv[2]);
	while (fread(&s,2,1,stdin)) {
	    if (n)
		printf(",");
	    if (((n++) % 8) == 0)
		printf("\n");
	    printf(" 0x%04X",s);
	}
	printf("\n};\n");
    }
    return 0;
}
