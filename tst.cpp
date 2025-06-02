#include <iostream>
#include <cmath>

void printArr(int* a, int s){
    std::cout << "[";

    for(int i = 0; i < s; i++){
        std::cout << a[i] << ", ";
    }

    std::cout << "]\n";
}

int main(){
    int a[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26};
    int s = sizeof(a)/sizeof(int);

    printArr(a, s);

    if(s % 2 == 1){
        a[s-2] += a[s-1];
        a[s-1] = 0;
        std::cout << "odd arr\n";
        s--;
        printArr(a, s);
        std::cout << "odd arr\n";
    }

    int reps = std::log2(s)+1;
    int offset = 1;
    int step = 2;

    for(int i = 0; i < reps; i++){
        std::cout << "step - " << step << " offset - " << offset << "\n";
        for(int j = 0; j < s; j++){
            if(j % step == 0 && j+offset < s){
                a[j] += a[j+offset];
                a[j+offset] = 0;
                printArr(a, s);
            }
        }
        std::cout << "end of i loop\n";

        offset *= 2;
        step *= 2;

    }
    

    std::cout << "dun!\n";
    
}