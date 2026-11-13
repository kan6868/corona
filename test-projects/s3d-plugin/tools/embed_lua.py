import sys
src=open(sys.argv[1],'rb').read()
out=['/* generated from s3d.lua - do not edit by hand */','static const char kS3DLua[] = {']
row=[]
for i,b in enumerate(src):
    row.append(str(b))
    if len(row)==24: out.append('  '+','.join(row)+','); row=[]
if row: out.append('  '+','.join(row)+',')
out.append('  0 };')
open(sys.argv[2],'w',encoding='utf-8').write('\n'.join(out)+'\n')
