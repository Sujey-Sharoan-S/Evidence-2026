/**
 * Note: The returned array must be malloced, assume caller calls free().
 */
int sum[2];
int* twoSum(int* nums, int numsSize, int target, int* returnSize) 
{

  for(int i=0;i<numsSize;i++)
  {
    for(int j=i+1;j<numsSize;j++)
    {
        if(target == nums[i]+nums[j])
        {
            sum[0]=i;
            sum[1]=j;
            *returnSize = 2;
             
        }
    }
    }

  return sum;
} 
    
