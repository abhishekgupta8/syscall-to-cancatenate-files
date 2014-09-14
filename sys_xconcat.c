#include <linux/linkage.h>
#include <linux/moduleloader.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/ctype.h>

asmlinkage extern long (*sysptr)(void *arg, int argslen);

struct dentry *dentry;

/* function for validating infiles/outfile */
int validate_files(__user const char **infiles, int infile_count,
		__user const char *outfile, int oflags, mode_t mode, int flags){
	
	struct file *filp_out, *filp_in;
	int i;

	#ifdef EXTRA_CREDIT
	int file_stat;
	mm_segment_t oldfs;
	struct kstat stat;
	struct file *filp_temp;
	#endif

	/* validating for same output/input file */
	filp_out = filp_open(outfile, O_RDONLY, 0);
	
	for(i=0;i<infile_count;i++){

		filp_in = filp_open(*(infiles + i), O_RDONLY, 0);
	
		if((!(!filp_out || IS_ERR(filp_out))) && 
			(!(!filp_in || IS_ERR(filp_in)))){	
			
			/* comparing inode numbers */
			if(filp_in->f_dentry->d_inode == 
				filp_out->f_dentry->d_inode){
				printk(
				"Error: Outfile and infile cannot be same\n");
				printk(
				"Error: Infile '%s' same as outfile '%s'\n", 
				(*(infiles+i)), outfile);
				return -EINVAL;	
			}
		}

		/* validating for correct permissions for infiles */
		if(!filp_in || IS_ERR(filp_in)){
			printk("Error: Infile '%s' cannot be accessed\n", 
				(*(infiles+i)));
			
			if(!(!filp_out || IS_ERR(filp_out)))
				filp_close(filp_out,NULL);
			
			return (int) PTR_ERR(filp_in);
		}
	
		filp_close(filp_in,NULL);
	}
	
	if(!(!filp_out || IS_ERR(filp_out)))
		filp_close(filp_out, NULL);

	#ifdef EXTRA_CREDIT
	/* checking for atomic mode and creating temp file */
	if((flags >> 2)== 1){

		oldfs = get_fs();
		set_fs(KERNEL_DS);
	
		file_stat = vfs_stat(outfile, &stat);

		if(file_stat<0){
			/*creating the temp file if outfile does not exist */
			filp_temp = filp_open("temp_xconcat", 
					O_WRONLY|oflags, mode);
			
			if(!filp_temp || IS_ERR(filp_temp)){
				printk(
				"Error: Outfile '%s' cannot be accessed\n", 
				outfile);
				set_fs(oldfs);			
				return (int) PTR_ERR(filp_temp);
			}
			/* saving dentry information for unlink/rename */
			dentry = filp_temp->f_dentry;		
		}	
		else{
			/* checking if mode is O_CREAT|O_EXCL 
			 and returning error if file exist in such mode */
			if(((oflags/077) == 03) ||
				((oflags/077) == 013) ||
				((oflags/077) == 023) ||
				((oflags/077) == 033)){
				printk(
				"Error: Outfile '%s' already exists\n",outfile);
				set_fs(oldfs);			
				return -EEXIST;
			}
			
			filp_temp = filp_open("temp_xconcat", 
					O_WRONLY|oflags|O_CREAT, stat.mode);
	
			
			if(!filp_temp || IS_ERR(filp_temp)){
				printk(
				"Error: Outfile '%s' cannot be accessed\n", 
				outfile);
				set_fs(oldfs);			
				return (int) PTR_ERR(filp_temp);
			}	
			/* saving dentry information for unlink/rename */
			dentry = filp_temp->f_dentry;			
		}	
		set_fs(oldfs);
		filp_close(filp_temp, NULL);
		
	}		
	#endif	

	return 0;
}

/* function to read files */
int wrapfs_read_file(__user const char *filename, void *buf, int len, 
			int large_file_it)
{
	struct file *filp;
	struct kstat stat;
	mm_segment_t oldfs;
	int bytes, file_stat;
	
	/* turning off address translation */
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	
	/* checking if file is empty */
	file_stat = vfs_stat(filename, &stat);

	if(file_stat<0){
		set_fs(oldfs);
		return -ENOENT;
	}

	if(stat.size==0){
		set_fs(oldfs);
		return -ENODATA;
	}
	
	filp = filp_open(filename, O_RDONLY, 0);
	
	if(large_file_it==0)
		filp->f_pos = 0;
	else
		filp->f_pos = (large_file_it * PAGE_SIZE);

	bytes = filp->f_op->read(filp, buf, len, &filp->f_pos);
	set_fs(oldfs);

	filp_close(filp, NULL);

	return bytes;
}

/* function to write files */
int wrapfs_write_file(struct file *filp_w, void *buf, int len, int pos)
{
	
	mm_segment_t oldfs;
	int ret;

	filp_w->f_pos = pos;
	
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	ret = vfs_write(filp_w, buf, len, &filp_w->f_pos);

	set_fs(oldfs);
	
	return ret;
}

asmlinkage long xconcat(void *arg, int argslen)
{
	/* structure to unpack user arguments */
	typedef struct args
	{
		__user const char *outfile;
		__user const char **infiles;
		unsigned int infile_count;
		int oflags;
		mode_t mode;
		unsigned int flags;
	}args;

	/* local variables */
	int i, read_ret = 0, write_ret=0, pos=0, num_of_files_w =0, tot_read=0, 	tot_write=0, large_file_it=0, val_in, copy_ret, error=0, 
	getname_counter=0;
	
	void *buf = NULL, *arguments = NULL;
	char *file = NULL, mode[4]; 
	struct file *filp_write;
	
	#ifdef EXTRA_CREDIT
	struct file *filp_new;	
	struct dentry *dentry_new;

	#endif

	/* checking for valid address of user parameters */
	if(arg == NULL){
		printk("Error: Passing arguments to kernel space failed\n");
		error = -EINVAL;
		goto final;
	}
		
	/* allocating memory for user arguments */
	arguments = kmalloc(argslen, __GFP_WAIT);
	
	if( arguments == NULL ){
		printk("Error: Memory allocation for user arguments failed\n");
		error = -ENOMEM;
		goto final;
	}
	
	/* copying arguments from user space and validating for bad addresses */
	copy_ret = copy_from_user(arguments, arg, argslen);
	
	if (copy_ret != 0){
		printk("Error: Arguments copy from user space failed\n");
		error = -EFAULT;
		goto free_arg;
	}
	
	/*checking if extra credit is defined or not 
	and returning error if called and not defined */
	#ifdef EXTRA_CREDIT
	if((((args *)arguments)->flags>> 2 ) == 1){
		printk("Running in atomic mode\n");	
	}
	
	#else
	if((((args *)arguments)->flags >> 2) == 1){
		printk("Error: Atomic mode not defined\n");
		error = -EPERM;
		goto free_arg;
	}
	#endif

	/* restricting max number of infiles to 10 */
	if(((args *)arguments)->infile_count >10 ){
		printk("Error: Maximum 10 infiles can be specified\n");
		error = -EPERM;
		goto free_arg;
	}

	/* validating out file for bad address */	
	file = getname(((args *)arguments)->outfile);
	
	if(IS_ERR(file)){
		printk("Error: Outfile name cannot be copied\n");
		error = (int) PTR_ERR(file);
		goto free_arg;
	}
	/* overwriting the same structure with kernel address of outfile */
	((args *)arguments)->outfile = file;
	
	/* validating in file for bad address */	
	for(i=0;i<((args *)arguments)->infile_count;i++){

		file = NULL;
		file = getname(((args *)arguments)->infiles[i]);
		
		if(IS_ERR(file)){
			printk("Error: Infile %s name cannot be copied\n",
				((args *)arguments)->infiles[i]);
			error = (int) PTR_ERR(file);
			goto free_file;
		}
	
	/* overwriting the same structure with kernel address of infile */
	((args *)arguments)->infiles[i] = file;	
	
	getname_counter++;
	}

	/* validating mode */	
	sprintf(mode,"%d",(((args *)arguments)->mode));	
	
	if(strlen(mode) > 4){
		error=-EINVAL;
		printk("Error: The arument to mode is invalid.\n");
		goto free_file;
	}
			
	for(i=0;i<strlen(mode);i++){
		if(!((isdigit(*(mode+i))) && (*(mode+i))<56)){
			error=-EINVAL;
			printk("Error: The argument to mode is invalid.\n");
			goto free_file;
		}
	}

	/* validating flags */
	if(!(((((args *)arguments)->flags) == 0x00) ||
		((((args *)arguments)->flags) == 0x01) ||
		((((args *)arguments)->flags) == 0x02) ||
		((((args *)arguments)->flags) == 0x04) ||
		((((args *)arguments)->flags) == 0x05) ||
		((((args *)arguments)->flags) == 0x06))) {
			printk("Error: Flags entered are invalid.\n");
			error = -EINVAL;
			goto free_file;
	}
	
	/* validating input/output files */
	val_in = validate_files(((args *)arguments)->infiles, 
				(((args *)arguments)->infile_count),
				(((args *)arguments)->outfile),
				(((args *)arguments)->oflags),
				(((args *)arguments)->mode),
				(((args *)arguments)->flags));

	if(val_in<0){
		error = val_in;			
		goto free_file;
	}
	
	/* alocating space for buffer */
	buf = kmalloc(PAGE_SIZE, __GFP_WAIT);
	
	/* looping through infiles to read */
	if(!((((args *)arguments)->flags >> 2) == 1)){

	filp_write = filp_open(((args *)arguments)->outfile, 
				((args *)arguments)->oflags|O_WRONLY, 
				((args *)arguments)->mode);
	
	if(!filp_write || IS_ERR(filp_write)){
			printk(
			"Error: Outfile '%s' cannot be accessed\n", 
			((args *)arguments)->outfile);
			error = (int) PTR_ERR(filp_write);
			goto free_buf;
		}		

	for(i=0;i<((args *)arguments)->infile_count;i++)
	{	
		/* reading input file data */
		read_ret =wrapfs_read_file(((args *)arguments)->infiles[i], 
						buf, PAGE_SIZE, large_file_it);
		
		/* checking for read errors */
		if ( read_ret < 0 ){
			
			if(read_ret == -ENOENT){
				printk("Error: No such file or directory\n");
				printk("Error occured for '%s' errno(%d)\n",
				((args *)arguments)->infiles[i], read_ret) ;
				continue;			
			}
			else if(read_ret == -ENODATA){
				num_of_files_w++;
				continue;
			}
			else{
				printk(
				"Error: Operation Failed for %s errno(%d)\n",
				((args *)arguments)->infiles[i], read_ret);
				continue;
			}
			read_ret=0;
		}
		else if(read_ret == PAGE_SIZE)
		{
			/* looping through same file again if bytes returned
			are more than page size */
			i--;
			large_file_it++;
			tot_read += read_ret;	
		}
		else{
			num_of_files_w++;
			tot_read += read_ret;
			large_file_it=0;
		}
		
		/* writing to outfile */
		write_ret = wrapfs_write_file(filp_write, buf, read_ret, pos);
		
		if(write_ret<0){
			error = write_ret;
			filp_close(filp_write, NULL);
			goto free_buf;
		}
		else{
			tot_write += write_ret;
		}

		pos += read_ret;
	}	
	filp_close(filp_write, NULL);
	}

	#ifdef EXTRA_CREDIT

	if((((args *)arguments)->flags >> 2) == 1){
	
	filp_write = filp_open("temp_xconcat", O_WRONLY, 0);
		
		/* appending the old outfile in case of append and create */
		if(!(((((args *)arguments)->oflags/0700) == 01) ||
			((((args *)arguments)->oflags/0700) == 03))){
		
		do{
			read_ret =wrapfs_read_file(
			((args *)arguments)->outfile, buf, PAGE_SIZE, 
			large_file_it);
	
			if((read_ret == -ENOENT) || (read_ret == -ENODATA)){
				break;
			}
			else if(read_ret < 0){
				printk(
				"Error: Outfile backup failed while reading\n");
				error = read_ret;
				filp_close(filp_write, NULL);
				goto free_temp;
			}	
		
			write_ret = wrapfs_write_file(filp_write, buf, 
							read_ret, pos);
		
			if(write_ret<0){
				printk(
				"Error: Outfile backup failed while writing\n");
				error = write_ret;	
				filp_close(filp_write, NULL);
				goto free_temp;
			}
			else{
				pos += write_ret;
				large_file_it++;
			}
			}while(read_ret == PAGE_SIZE);
		}
	
		if(!((((args *)arguments)->oflags/0700) == 02)){
			pos = 0;
		}

		read_ret = 0;
		write_ret = 0;
		large_file_it = 0;
 		
		/* looping through infiles */
		for(i=0;i<((args *)arguments)->infile_count;i++)
		{
			/* reading infile */
			read_ret =wrapfs_read_file(
					((args *)arguments)->infiles[i], buf, 
					PAGE_SIZE, large_file_it);
			
			/* checking for possible errors */						if(read_ret == -ENODATA){
				num_of_files_w++;
				continue;
			}
			else if(read_ret <0){
				printk(
				"Error: Reading file %s failed\n", 
				((args *)arguments)->infiles[i]);
				error = read_ret;
				filp_close(filp_write, NULL);		
				goto free_temp;
			}		
			else if(read_ret == PAGE_SIZE)
			/* looping through same file again if bytes returned
			are more than page size */		
			{
				i--;
				large_file_it++;
				tot_read += read_ret;	
			}
			else{
				num_of_files_w++;
				tot_read += read_ret;
				large_file_it=0;
			}
			
			/* writing to temp outfile */
			write_ret = wrapfs_write_file(filp_write, buf, 
							read_ret, pos);
			
			if(write_ret<0){
				printk(
				"Error: Writing to backup file failed\n");
				error = write_ret;
				filp_close(filp_write, NULL);
				goto free_temp;
			}
			else{
				tot_write += write_ret;
			}	

			pos += read_ret;
		}	
		
		filp_close(filp_write, NULL);
		
	}
	#endif

	/* setting appropriate return number according to flag selected */
	if(((args *)arguments)->flags == 0x00){
		printk("Total bytes sucessfully written: ");
		error = tot_write;
		goto free_buf;
	}
	else if(((args *)arguments)->flags == 0x01){
		printk("Total files successfully written: ");
		error = num_of_files_w;
		goto free_buf;
	}
	else if(((args *)arguments)->flags == 0x02){
		printk("Percentage of data written: ");
		if(tot_read == 0){
			error = 0;
			goto free_buf;
		}
		else{
			error = (tot_write/tot_read*100);
			goto free_buf;
		}
	}	
	#ifdef EXTRA_CREDIT	
	else if(((args *)arguments)->flags == 0x04){
		printk("Total bytes sucessfully written: ");
		error = tot_write;
		goto free_temp;
	}
	else if(((args *)arguments)->flags == 0x05){
		printk("Total files successfully written: ");
		error = num_of_files_w;
		goto free_temp;
	}
	else if(((args *)arguments)->flags == 0x06){
		printk("Percentage of data written: ");
		if(tot_read == 0){
			error = 0;
			goto free_temp;
		}
		else{
			error = (tot_write/tot_read*100);
			goto free_temp;
		}
	}
	
	free_temp:
		/* removing temp file as error occured */
		if(error < 0){
			mutex_lock(&dentry->d_parent->d_inode->i_mutex);
			if((vfs_unlink(dentry->d_parent->d_inode, dentry))< 0 ){
				printk("WARNING! vfs_unlink failed\n");
				printk("Unexpected error occured\n");
			}	
			mutex_unlock(&dentry->d_parent->d_inode->i_mutex);
		}
		/* renaming temp file as sucessfully file is written */
		else{
			
			filp_new = filp_open(((args *)arguments)->outfile, 
					O_WRONLY|((args *)arguments)->oflags,
					((args *)arguments)->mode);
			dentry_new = filp_new->f_dentry;
			filp_close(filp_new, NULL);

			lock_rename(dentry->d_parent, dentry_new->d_parent);
			if((vfs_rename(dentry->d_parent->d_inode, dentry, 
				dentry_new->d_parent->d_inode, dentry_new))< 0){
				printk("WARNING! vfs_rename failed\n");
				printk("Unexpected error occured\n");
			}
			unlock_rename(dentry->d_parent, dentry_new->d_parent);
		}	
	#endif

	free_buf: 
		kfree(buf);

	free_file:
		putname(((args *)arguments)->outfile);

		for(i=0;i<getname_counter;i++){
			if(!(((args *)arguments)->infiles[i] == NULL ))
				putname(((args *)arguments)->infiles[i]);
		}
	
	free_arg:
		kfree(arguments);

	final:
		return error;
	
}

static int __init init_sys_xconcat(void)
{
	printk("installed new sys_xconcat module\n");
	if (sysptr == NULL)
		sysptr = xconcat;
	return 0;
}
static void  __exit exit_sys_xconcat(void)
{
	if (sysptr != NULL)
		sysptr = NULL;
	printk("removed sys_xconcat module\n");
}
module_init(init_sys_xconcat);
module_exit(exit_sys_xconcat);
MODULE_LICENSE("GPL");
