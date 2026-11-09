int singleNumber(int* nums, int numsSize) 
{
    int i,single = 0;
    for(i=0;i<numsSize;i++)
    {
        single=single^nums[i];

    }
    return single;
}
/*
this is a my solution with O(n^2) time complexity and O(1) space complexity
for(i = 0; i < numsSize; i++)
{
    for(j = 0; j < numsSize; j++)
    {   
        single = 0;

        if(nums[i] == nums[j])
        {
            single++;   
        }
    }

    if(single == 1)
        z = nums[i];
}
*/