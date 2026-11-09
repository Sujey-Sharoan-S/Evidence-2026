int lengthOfLastWord(char* s) 
{
    int i,len;
    len = strlen(s);
    int count = 0;
    for(i=len-1;i>=0;i--)
    {
        if(s[i]==' ' && count == 0)
        {
            continue;
           
        }
        else if(s[i]!=' ')
        {
            count++;
            
        }
        else
        break;
        

    }
    return count;
}