#include "inode_manager.h"
#include "gettime.h"

// disk layer -----------------------------------------

disk::disk()
{
  bzero(blocks, sizeof(blocks));
}

void
disk::read_block(blockid_t id, char *buf)
{
  if (id < 0 || id >= BLOCK_NUM || buf == NULL)
    return;

  memcpy(buf, blocks[id], BLOCK_SIZE);
}

void
disk::write_block(blockid_t id, const char *buf)
{
  if (id < 0 || id >= BLOCK_NUM || buf == NULL)
    return;

  memcpy(blocks[id], buf, BLOCK_SIZE);
}

// block layer -----------------------------------------

//set bmp
uint32_t
block_manager::set_bmp(uint32_t i,uint32_t flag)
{  
  bitmap[i/(8*sizeof(uint32_t))] |= flag<<(i%(8*sizeof(uint32_t)));
  return 1;
}

//check bmp
uint32_t
block_manager::check_bmp(uint32_t i,uint32_t j)
{
   return bitmap[i]>>j&1; 
}


// Allocate a free disk block.
blockid_t
block_manager::alloc_block()
{
  /*
   * your lab1 code goes here.
   * note: you should mark the corresponding bit in block bitmap when alloc.
   * you need to think about which block you can start to be allocated.
   */
  blockid_t block_num;
  if(p_bmp<=BLOCK_SIZE){
	set_bmp(p_bmp,1);  
	block_num = p_bmp;
	p_bmp++;
	return block_num;
  }else{
	for(int i=0;i<MAXMAP;i++){
		if(bitmap[i] != 0xffffff){
			for(int j=0;j<32;j++){
				if(check_bmp(i,j)==0){
					block_num = i*32+j;
					set_bmp(block_num,1);
					return block_num;
				}			
		}
	  }
	}
					
	  
  }
  return 0;
}

void
block_manager::free_block(uint32_t id)
{
  /* 
   * your lab1 code goes here.
   * note: you should unmark the corresponding bit in the block bitmap when free.
   */
  set_bmp(id,0);
  
  return;
}

// The layout of disk should be like this:
// |<-sb->|<-free block bitmap->|<-inode table->|<-data->|
block_manager::block_manager()
{
  d = new disk();

  // format the disk
  sb.size = BLOCK_SIZE * BLOCK_NUM;
  sb.nblocks = BLOCK_NUM;
  sb.ninodes = INODE_NUM;
  p_bmp =INODE_NUM/IPB + BLOCK_NUM/BPB +3;
  memset(bitmap,0,sizeof(uint32_t)*MAXMAP);
  for(int i = 0;i<p_bmp;i++) set_bmp(i,1);
}

void
block_manager::read_block(uint32_t id, char *buf)
{
  d->read_block(id, buf);
}

void
block_manager::write_block(uint32_t id, const char *buf)
{
  d->write_block(id, buf);
}

// inode layer -----------------------------------------

inode_manager::inode_manager()
{
  bm = new block_manager();
  last_inum = 0;
  restblocks = BLOCK_NUM;
  uint32_t root_dir = alloc_inode(extent_protocol::T_DIR);
  if (root_dir != 1) {
    printf("\tim: error! alloc first inode %d, should be 1\n", root_dir);
    exit(0);
  }
}

/* Create a new file.
 * Return its inum. */
uint32_t
inode_manager::alloc_inode(uint32_t type)
{
  /* 
   * your lab1 code goes here.
   * note: the normal inode block should begin from the 2nd inode block.
   * the 1st is used for root_dir, see inode_manager::inode_manager().
   */
	struct timespec tp;
	if(clock_gettime(CLOCK_REALTIME,&tp)== -1){
		return -1;
	}	
	last_inum ++;
	struct inode *ino;
	ino = (struct inode*)malloc(sizeof(struct inode));
	ino->ctime = ino->atime = ino->mtime = tp.tv_sec;
	ino->type = type;
	ino->used_blocks = 0;
	ino->size = 0;
	put_inode(last_inum,ino);
	return last_inum;
}

void
inode_manager::free_inode(uint32_t inum)
{
  /* 
   * your lab1 code goes here.
   * note: you need to check if the inode is already a freed one;
   * if not, clear it, and remember to write back to disk.
   */
  struct inode *ino = get_inode(inum);
  if(ino){
	  if(ino->used_blocks > NDIRECT){
		  ino->used_blocks = NDIRECT;
		  free_inode(ino->blocks[NDIRECT]);
	  } 
	  for(int i=0;i<ino->used_blocks;i++){
		  bm->free_block(ino->blocks[i]);
	  }
	  delete ino;
  }
 
  return;
}


/* Return an inode structure by inum, NULL otherwise.
 * Caller should release the memory. */
struct inode* 
inode_manager::get_inode(uint32_t inum)
{
  struct inode *ino, *ino_disk;
  char buf[BLOCK_SIZE];

  printf("\tim: get_inode %d\n", inum);

  if (inum < 0 || inum >= INODE_NUM) {
    printf("\tim: inum out of range\n");
    return NULL;
  }

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  // printf("%s:%d\n", __FILE__, __LINE__);

  ino_disk = (struct inode*)buf + inum%IPB;
  if (ino_disk->type == 0) {
    printf("\tim: inode not exist\n");
    return NULL;
  }

  ino = (struct inode*)malloc(sizeof(struct inode));
  *ino = *ino_disk;

  return ino;
}

void
inode_manager::put_inode(uint32_t inum, struct inode *ino)
{
  char buf[BLOCK_SIZE];
  struct inode *ino_disk;

  printf("\tim: put_inode %d\n", inum);
  if (ino == NULL)
    return;

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  ino_disk = (struct inode*)buf + inum%IPB;
  *ino_disk = *ino;
  bm->write_block(IBLOCK(inum, bm->sb.nblocks), buf);
}

#define MIN(a,b) ((a)<(b) ? (a) : (b))

/* Get all the data of a file by inum. 
 * Return alloced data, should be freed by caller. */
void
inode_manager::read_file(uint32_t inum, char **buf_out, int *size)
{
  /*
   * your lab1 code goes here.
   * note: read blocks related to inode number inum,
   * and copy them to buf_Out
   */
  struct inode *ino = get_inode(inum);
  if(ino){
	*size = ino->size;
	*buf_out = new char[*size];
	char *buf = *buf_out;
	while(true){
	   int x = ino->used_blocks > NDIRECT ? NDIRECT:ino->used_blocks;
		   char *block_buff;
		   for(int i=0;i < x;i++){		
			   bm->read_blocks(ino->blocks[i],block_buff);
			   buf += block_buff;
			}
		   if ( x < NDIRECT ) break;
		   ino = get_inode(ino->blocks[NDIRECT]);
	}
  }
  
  return;
}

/* alloc/free blocks if needed */
void
inode_manager::write_file(uint32_t inum, const char *buf, int size)
{
  /*
   * your lab1 code goes here.
   * note: write buf to blocks of inode inum.
   * you need to consider the situation when the size of buf 
   * is larger or smaller than the size of original inode
   */
  struct inode *ino = get_inode(inum);
  if (ino->used_blocks > 0){
	//free used blocks
	if (ino->used_blocks > NDIRECT){
		free_inode(ino->blocks[NDIRECT]);
		ino->used_blocks = NDIRECT;
	}
	for(int i=0;i<used_blocks;i++)	bm->free_block(ino->blocks[i]);
  }
  //alloc new blocks
  int need_block = size % BLOCK_SIZE>0 ? size / BLOCK_SIZE+1 : size / BLOCK_SIZE;
  if (restblocks - need_block < 0 ){
	  return;
  }
  ino->size = size;
  restblocks -= need_block;
  int need_block2=0;
  if(need_block > NDIRECT){
	need_block2 = need_block-NDIRECT;
	need_block = NDIRECT;
  }  
  ino->used_blocks = need_block+1;
  uint32_t a_block;
  for (int i=0;i<need_block;i++){
	a_block = bm->alloc_block();
	ino->blocks[i] = a_block;
	bm->write_block(a_block,buf+i*BLOCK_SIZE);
  }
  if(need_block2){
	  uint32_t new_file = alloc_inode(extent_protocol::T_FILE);
	  ino_blocks[NDIRECT]=new_file;
	  write_file(new_file,buf+NDIRECT*BLOCK_SIZE,size-NDIRECT*BLOCK_SIZE);
  }
  return;
}

void
inode_manager::getattr(uint32_t inum, extent_protocol::attr &a)
{
  /*
   * your lab1 code goes here.
   * note: get the attributes of inode inum.
   * you can refer to "struct attr" in extent_protocol.h
   */
  struct inode *node = get_inode(inum);
  a.size = node->size;
  a.atime = node->atime;
  a.mtime = node->mtime;
  a.ctime = node->ctime;
  a.type = node->type;  
  return;
}

void
inode_manager::remove_file(uint32_t inum)
{
  /*
   * your lab1 code goes here
   * note: you need to consider about both the data block and inode of the file
   */
  
  return;
}
