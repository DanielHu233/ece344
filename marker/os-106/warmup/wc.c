#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "common.h"
#include "wc.h"

//the function to calculate hash value
/*int hashCode(const char* str, int len){
    unsigned int hash = 0, i = 0, x = 0;
    for(i = 0;i < len;i++){
        hash = (hash << 4) + (*str);
        if((x = hash & 0xF0000000L) != 0){
            hash ^= (x >> 24);
        }
        hash &= ~x;
    }
    return (int)hash;
}*/
unsigned long long int hashCode(char* str, int len){
    unsigned long long int result = 0;
    char* p;
    for(p = str;*p;p++){
        result = result*31 + *p;
    }
    return result;
}

//the hash node struct
typedef struct hash_node{
    struct hash_node* next;
    char* key;
    int* val;
}HashNode;

//the hash table struct
struct wc {
	/* you can define this struct to have whatever fields you want. */
    HashNode** head;
    long size;
};

struct wc *
wc_init(char *word_array, long size)
{
    //printf("%ld***************", size);
	struct wc *wc;

	wc = (struct wc *)malloc(sizeof(struct wc));
	assert(wc);


        //create hash table
        wc->size = size/4;
        wc->head = (HashNode**)malloc(wc->size * sizeof(HashNode*));
        if(wc->head == NULL){
            printf("fail to allocated space.\n");
            //return wc;
        }
        for(long i = 0;i < wc->size;i++){
            wc->head[i] = NULL;
        }
        
        long left = 0, right = 0;
        while(right < size){
            //separate words
            if(isspace(word_array[right])){
                //when encounters a space
                int word_size = right - left;
                char* new_word = (char*)malloc((word_size+1) * sizeof(char));
                for(int j = 0;j < word_size;j++){
                    new_word[j] = word_array[left + j];
                }
                new_word[word_size] = '\0';
                right++;
                left = right;
                            
                //now update the hash table
                unsigned long long int index = hashCode(new_word, word_size);
                //int index = right;
                if(index == 0){
                    free(new_word);
                    continue;
                }
                
                int place = index % wc->size;
                
                //if collision exists         
                HashNode* ptr = wc->head[place];
                HashNode* pptr = NULL;

                //check if this word is already added into the hash table
                while(ptr && (strcmp(ptr->key, new_word) != 0)){
                    pptr = ptr;
                    ptr = ptr->next;
                }
                //already exists
                if(ptr){
                    *(ptr->val) = *(ptr->val) + 1;
                    free(new_word);
                //does not exist
                }else{
                    ptr = (HashNode*)malloc(sizeof(HashNode));
                    ptr->key = new_word;
                    new_word = NULL;
                    ptr->val = (int*)malloc(sizeof(int));
                    ptr->next = NULL;
                    *(ptr->val) = 1;
                    
                    if(!pptr){
                        wc->head[place] = ptr;
                    }else{
                        pptr->next = ptr;
                    }
                }
                           
            }else{
                right++;   
            }
        }

	return wc;
}

void
wc_output(struct wc *wc)
{
    int size = wc->size;
    for(int i = 0 ; i < size ; i++){
        HashNode* ptr = wc->head[i];
        while(ptr){
            printf("%s:%d\n", ptr->key, *(ptr->val));
            ptr = ptr->next;
        }
    }
}

void
wc_destroy(struct wc *wc)
{
    for(int i = 0;i < wc->size;i++){
        //if there's node here
        if(wc->head[i]){
            HashNode* ptr = wc->head[i];
            while(ptr->next){
                HashNode* temp = ptr->next;
                free(ptr->key);
                free(ptr->val);
                free(ptr);
                ptr = temp;
            }
            free(ptr->key);
            free(ptr->val);
            free(ptr);
        }
    }
    free(wc->head);
    free(wc);
}
