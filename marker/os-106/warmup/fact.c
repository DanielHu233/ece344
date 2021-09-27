#include "common.h"
#include <stdbool.h>

bool check_int(char* num);
int factorial(int nums);

int
main(int argc, char **argv)
{
    if(argc == 1){
        printf("Huh?\n");
    }else{
        if(check_int(argv[1])){
            if(atoi(argv[1]) <= 0){
                printf("Huh?\n");
            }else if(atoi(argv[1]) > 12){
                printf("Overflow\n");
            }else{
                int result = factorial(atoi(argv[1]));
                printf("%d\n", result);
            }
        }else{
            printf("Huh?\n");
        }
    }
	return 0;
}

int factorial(int nums){
    if(nums == 1) return 1;
    return nums*factorial(nums-1);
}

bool check_int(char* num){
    for(int i = 0;num[i];i++){
        if((num[i] >= '0' && num[i] <= '9') || num[i] == '-'){
            if(num[i] == '-'){
                if(i != 0){
                    return false;
                }
            }
            continue;
        }else{
            return false;
        }
    }
    return true;
}

