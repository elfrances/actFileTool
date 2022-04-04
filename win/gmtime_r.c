struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
    return (gmtime_s(result, timep) == 0) ? result : (void *) 0;
}


