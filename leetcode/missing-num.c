int missingNumber(int* nums, int numsSize)
{
    int missing = numsSize;
    for(int i=0;i<numsSize;i++)
    {
        missing = missing + i - nums[i];
    }
    return missing;
}