extern int	errno;

listen(s, backlog)
int	s, backlog;
{
	int	a;
	if ((a = _listen(s, backlog)) == -1)
		maperror(errno);
	return(a);
}


