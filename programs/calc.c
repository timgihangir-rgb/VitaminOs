
void sys_print(const char* s);
void sys_readline(char* buf, int max);
void sys_print_int(int n);
int str_to_int(const char* str, int* offset);

void main() {
    sys_print("\n[VitaminOS Math Layer]\n");
    sys_print("Enter expression (e.g. 25 + 17): ");

    char input[64];
    for (int i = 0; i < 64; i++) input[i] = 0;
    sys_readline(input, 60);

    int i = 0;
    int offset = 0;

    int num1 = str_to_int(&input[i], &offset);
    i += offset;

    while (input[i] == ' ') i++;

    char op = input[i];
    i++;

    int num2 = str_to_int(&input[i], &offset);

    int result = 0;
    int err = 0;

    if (op == '+') {
        result = num1 + num2;
    } else if (op == '-') {
        result = num1 - num2;
    } else if (op == '*') {
        result = num1 * num2;
    } else if (op == '/') {
        if (num2 == 0) err = 1;
        else result = num1 / num2;
    } else {
        err = 2;
    }

    if (err == 1) {
        sys_print("Error: Division by zero!\n");
    } else if (err == 2) {
        sys_print("Error: Unknown operator or format!\n");
    } else {
        sys_print("Result: ");
        sys_print_int(result);
        sys_print("\n");
    }

    return;
}


int str_to_int(const char* str, int* offset) {
    int res = 0;
    int sign = 1;
    int i = 0;

    while (str[i] == ' ') i++;

    if (str[i] == '-') {
        sign = -1;
        i++;
    }

    while (str[i] >= '0' && str[i] <= '9') {
        res = res * 10 + (str[i] - '0');
        i++;
    }

    if (offset) *offset = i;
    return res * sign;
}

void sys_print(const char* s) {
    asm volatile("movl $1, %%eax; movl %0, %%ebx; int $0x40" : : "r"(s) : "eax", "ebx");
}

void sys_readline(char* buf, int max) {
    asm volatile("movl $2, %%eax; movl %0, %%ebx; movl %1, %%ecx; int $0x40" : : "r"(buf), "r"(max) : "eax", "ebx", "ecx");
}


void sys_print_int(int n) {
    asm volatile("movl $3, %%eax; movl %0, %%ebx; int $0x40" : : "r"(n) : "eax", "ebx");
}
