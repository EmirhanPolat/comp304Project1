#include <stdio.h>
#include <unistd.h>

int main() {
	  char *filename = "./output";
	  
	  

// Use: dup2, fileno, fopen, STDOUT_FILENO, filename
//   // Goal: Without changing the printf statment, and only using the above
//     // functions, and variables make the string "I should be in a file" appear in a file called    // "./output".

// Writing code begins here
	  FILE *fptr = 	fopen(filename, "w");
	  int fileNo = fileno(fptr);
	  
	  
	  dup2(fileNo, STDOUT_FILENO);
  	  		
// Writing code ends here
	    
	 printf("I should be in a file\n");
 }
		
