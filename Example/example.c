#include <stddef.h>

typedef struct ListNode_t {
    float data;
    struct ListNode_t *next;
} ListNode;

float update_density(float, float);
size_t calc_index(float data);

void do_stuff(ListNode *head, float* density) {
    ListNode *node = head;
    while (node != NULL) {
        size_t index = calc_index(node->data);
        density[index] = update_density(density[index], node->data);
        node = node->next;
    }
}
