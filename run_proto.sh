dirname="proto"
fname="helloworld"
protoc -I ./${dirname} --grpc_out=./${dirname} --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` ./${dirname}/${fname}.proto
protoc -I ./${dirname} --cpp_out=./${dirname} ./${dirname}/${fname}.proto
cd ${dirname}
mv ${fname}.grpc.pb.* ../
mv ${fname}.pb.* ../
