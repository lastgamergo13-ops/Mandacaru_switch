#!/usr/bin/env python3
# Mini analisador de SPIR-V (sem dependencias) para os arquivos .spv que o emulador grava quando o driver recusa um pipeline
# (shader_dumps/<titleId>/failed/ -> botao de exportar da tela de logs).
# Uso: python3 spv_inspect.py arquivo.spv [deslocamento_em_bytes] [id]
#   deslocamento: mostra a instrucao que contem esse byte (o numero que o driver escreve em "SPIR-V offset N")
#   id: lista as instrucoes que mencionam esse id (o "id N" da mensagem do driver)
# Exemplo (No Man's Sky): python3 spv_inspect.py shader.spv 12812 807  -> mostra um OpConstant sem tipo (constante double em GPU sem float64)
import struct, sys
OPS={0:'Nop',1:'Undef',2:'SourceContinued',3:'Source',4:'SourceExtension',5:'Name',6:'MemberName',7:'String',8:'Line',10:'Extension',11:'ExtInstImport',12:'ExtInst',14:'MemoryModel',15:'EntryPoint',16:'ExecutionMode',17:'Capability',19:'TypeVoid',20:'TypeBool',21:'TypeInt',22:'TypeFloat',23:'TypeVector',24:'TypeMatrix',25:'TypeImage',26:'TypeSampler',27:'TypeSampledImage',28:'TypeArray',29:'TypeRuntimeArray',30:'TypeStruct',32:'TypePointer',33:'TypeFunction',41:'ConstantTrue',42:'ConstantFalse',43:'Constant',44:'ConstantComposite',46:'ConstantNull',54:'Function',55:'FunctionParameter',56:'FunctionEnd',57:'FunctionCall',59:'Variable',61:'Load',62:'Store',65:'AccessChain',71:'Decorate',72:'MemberDecorate',74:'DecorationGroup',77:'VectorExtractDynamic',79:'VectorShuffle',80:'CompositeConstruct',81:'CompositeExtract',82:'CompositeInsert',86:'SampledImage',87:'ImageSampleImplicitLod',88:'ImageSampleExplicitLod',89:'ImageSampleDrefImplicitLod',90:'ImageSampleDrefExplicitLod',91:'ImageSampleProjImplicitLod',94:'ImageSampleProjDrefImplicitLod',95:'ImageFetch',96:'ImageGather',97:'ImageDrefGather',98:'ImageRead',99:'ImageWrite',100:'Image',103:'ImageQuerySizeLod',104:'ImageQuerySize',105:'ImageQueryLod',106:'ImageQueryLevels',107:'ImageQuerySamples',109:'ConvertFToU',110:'ConvertFToS',111:'ConvertSToF',112:'ConvertUToF',113:'UConvert',114:'SConvert',115:'FConvert',116:'QuantizeToF16',124:'Bitcast',126:'SNegate',127:'FNegate',128:'IAdd',129:'FAdd',130:'ISub',131:'FSub',132:'IMul',133:'FMul',134:'UDiv',135:'SDiv',136:'FDiv',137:'UMod',139:'SMod',140:'FRem',141:'FMod',142:'VectorTimesScalar',143:'MatrixTimesScalar',144:'VectorTimesMatrix',145:'MatrixTimesVector',146:'MatrixTimesMatrix',148:'Dot',164:'Any',165:'All',166:'IsNan',167:'IsInf',169:'LogicalEqual',170:'LogicalNotEqual',171:'LogicalOr',172:'LogicalAnd',173:'LogicalNot',174:'Select',175:'IEqual',176:'INotEqual',177:'UGreaterThan',178:'SGreaterThan',179:'UGreaterThanEqual',180:'SGreaterThanEqual',181:'ULessThan',182:'SLessThan',183:'ULessThanEqual',184:'SLessThanEqual',185:'FOrdEqual',186:'FUnordEqual',187:'FOrdNotEqual',188:'FUnordNotEqual',189:'FOrdLessThan',190:'FUnordLessThan',191:'FOrdGreaterThan',192:'FUnordGreaterThan',193:'FOrdLessThanEqual',194:'FUnordLessThanEqual',195:'FOrdGreaterThanEqual',196:'FUnordGreaterThanEqual',194:'FUnordLessThanEqual',197:'ShiftRightLogical',198:'ShiftRightArithmetic',199:'ShiftLeftLogical',200:'BitwiseOr',201:'BitwiseXor',202:'BitwiseAnd',203:'Not',204:'BitFieldInsert',205:'BitFieldSExtract',206:'BitFieldUExtract',207:'BitReverse',208:'BitCount',215:'DPdx',216:'DPdy',217:'Fwidth',245:'Phi',246:'LoopMerge',247:'SelectionMerge',248:'Label',249:'Branch',250:'BranchConditional',251:'Switch',252:'Kill',253:'Return',254:'ReturnValue',255:'Unreachable',320:'GroupNonUniformElect',4416:'ExtInst4416'}
b=open(sys.argv[1],'rb').read(); w=struct.unpack('<%dI'%(len(b)//4),b)
magic,ver,gen,bound,_=w[:5]; print('version',hex(ver),'bound',bound, 'words',len(w))
i=5; target=int(sys.argv[2]) if len(sys.argv)>2 else None; tid=int(sys.argv[3]) if len(sys.argv)>3 else None
instrs=[]
while i<len(w):
    n=w[i]>>16; op=w[i]&0xffff
    if n==0: print('bad word count at byte',i*4); break
    instrs.append((i*4,op,w[i:i+n])); i+=n
print('instructions',len(instrs))
def show(k):
    off,op,ws=instrs[k]; print(' @%d %s %s'%(off,OPS.get(op,'Op%d'%op),[hex(x) if x>1000000 else x for x in ws[1:]]))
if target is not None:
    for k,(off,op,ws) in enumerate(instrs):
        if off<=target<off+len(ws)*4:
            print('--- instruction containing byte',target); 
            for j in range(max(0,k-4),min(len(instrs),k+3)): print('>>' if j==k else '  ',end=''); show(j)
if tid is not None:
    print('--- instructions mentioning id',tid)
    defined=False
    for k,(off,op,ws) in enumerate(instrs):
        if tid in ws[1:]:
            show(k)
