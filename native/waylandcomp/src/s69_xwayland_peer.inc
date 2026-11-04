/* Only included by compositor.c. Check the opened, executing R4+resize ELF
 * on connect; not in render/commit. No private platform crypto API/child process. */
static uint32_t s69_ror(uint32_t x, unsigned n) {return (x>>n)|(x<<(32-n));}
static void s69_sha_block(uint32_t state[8], const unsigned char p[64]) {
    static const uint32_t k[64]={
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64],a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
    for(unsigned i=0;i<16;i++)w[i]=((uint32_t)p[4*i]<<24)|((uint32_t)p[4*i+1]<<16)|((uint32_t)p[4*i+2]<<8)|(uint32_t)p[4*i+3];
    for(unsigned i=16;i<64;i++){uint32_t x=w[i-15],y=w[i-2];w[i]=w[i-16]+(s69_ror(x,7)^s69_ror(x,18)^(x>>3))+w[i-7]+(s69_ror(y,17)^s69_ror(y,19)^(y>>10));}
    for(unsigned i=0;i<64;i++){
        uint32_t t=h+(s69_ror(e,6)^s69_ror(e,11)^s69_ror(e,25))+((e&f)^(~e&g))+k[i]+w[i];
        uint32_t u=(s69_ror(a,2)^s69_ror(a,13)^s69_ror(a,22))+((a&b)^(a&c)^(b&c));
        h=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;
    }
    state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
}
static int s69_elf_hash(int fd,off_t size,const uint32_t target[8]) {
    if(size<0 || size>32*1024*1024)return 0;
    uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    unsigned char q[16384];size_t kept=0;off_t pos=0;
    while(pos<size){
        size_t want=sizeof(q)-kept;if((off_t)want>size-pos)want=(size_t)(size-pos);
        ssize_t n=pread(fd,q+kept,want,pos);if(n<0&&errno==EINTR)continue;if(n<=0)return 0;
        size_t got=kept+(size_t)n;pos+=n;kept=got%64;
        for(size_t i=0;i<got-kept;i+=64)s69_sha_block(h,q+i);
        if(kept)memmove(q,q+got-kept,kept);
    }
    uint64_t bits=(uint64_t)size*8;q[kept++]=128;
    size_t end=kept<=56?64:128;memset(q+kept,0,end-kept);
    for(unsigned i=0;i<8;i++)q[end-1-i]=(unsigned char)(bits>>(8*i));
    s69_sha_block(h,q);if(end==128)s69_sha_block(h,q+64);
    return memcmp(h,target,8*sizeof(*h))==0;
}
static int s69_same_stamp(const struct stat *a,const struct stat *b){
    return a->st_dev==b->st_dev && a->st_ino==b->st_ino && a->st_size==b->st_size &&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int s69_scoped_xwl(const char *proc,const struct stat *actual){
    static const char *const fixed[]={
        "/data/data/com.termux/files/home/alroot_wayland/.source/gpu-stage1/s69-managed-resize/runtime/Xwayland",
        "/data/data/com.termux/files/home/alroot_wayland/.source/gpu-stage1/s79-glx-dri3/direct-s83-r5/runtime/Xwayland"
    };
    static const uint32_t digests[2][8]={
        {0x89b686ed,0xc57e668b,0x6f2a7dc6,0x29053acc,0x26136ab8,0x148f0645,0x966391cb,0x195da2f6},
        {0x2226b2a2,0x50cfc7bd,0xf6439bc3,0x153bd61b,0xfb25348b,0x09429f45,0x26cdd7a2,0x7716b7a8}
    };
    static struct stat verified[2];
    static int have_verified[2]; /* Wayland thread only; one slot per exact path. */
    for(unsigned i=0;i<2;++i){
        struct stat pinned,fdstat,after;
        if(lstat(fixed[i],&pinned)||!S_ISREG(pinned.st_mode)||pinned.st_uid!=getuid()||
           (pinned.st_mode&0022)||!s69_same_stamp(actual,&pinned))continue;
        int fd=open(proc,O_RDONLY|O_CLOEXEC);if(fd<0)return 0;
        int ok=!fstat(fd,&fdstat)&&s69_same_stamp(&fdstat,&pinned);
        if(ok && (!have_verified[i]||!s69_same_stamp(&fdstat,&verified[i]))){
            ok=s69_elf_hash(fd,fdstat.st_size,digests[i]) && !fstat(fd,&after) && s69_same_stamp(&fdstat,&after);
            if(ok){verified[i]=fdstat;have_verified[i]=1;}
        }
        close(fd);return ok?(int)i+1:0;
    }
    return 0;
}

/* An active wl_client plus the /proc executable is authoritative; do not
 * remember a stale last PID or infer admission from a pathname alone. */
static int s69_live_peer(pid_t wanted, unsigned long long *ino) {
    if(wanted<=0)return 0;
    for(struct client_info *ci=g_clients;ci;ci=ci->next){
        if(ci->pid!=wanted||!ci->s65_root_peer)continue;
        char path[64];struct stat st;snprintf(path,sizeof(path),"/proc/%d/exe",(int)wanted);
        if(!stat(path,&st)){int variant=s69_scoped_xwl(path,&st);if(variant){*ino=(unsigned long long)st.st_ino;return variant;}}
    }
    return 0;
}
