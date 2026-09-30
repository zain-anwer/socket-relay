#include "crypto_utils.h"

void encrypt (char* text, int key) 
{
    for (int i = 0 ; text[i] != '\0' ; i++) 
    {
        unsigned char character = (unsigned char)text[i];
        if (isalpha(character))
        {
            char base = islower(character) ? 'a' : 'A';
            text[i] = (character - (unsigned char)base + key) % 26 + base;
        }
    }
}

void decrypt (char* text, int key) 
{	encrypt(text, 26 - (key % 26));	} 

