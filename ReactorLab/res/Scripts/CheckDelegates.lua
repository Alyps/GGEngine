local W=require("res.Scripts.World")
local M={}
function M.on_init(s)
 print("BEGIN Delegates")
 local e=Delegate.new(s)
 local token,count={},0
 local sub=e:connect(s,function(self,...)
  local a,b,c=...
  count=count+1
  W.check("payload identity nil arity",select("#",...)==3 and a==token and b==7 and c==nil)
 end)
 W.check("emit live",e:emit(token,7,nil))
 W.check("disconnect idempotent",sub:disconnect() and not sub:disconnect())
 e:emit();W.check("disconnected skipped",count==1)
 local nested=Delegate.new(s);local depth=0
 nested:connect(s,function(self,n) depth=depth+1;if n>0 then nested:emit(n-1) end end)
 nested:emit(2);W.check("recursive emission",depth==3)
 local ordered=Delegate.new(s);local second;local calls=0
 ordered:connect(s,function() second:disconnect() end)
 second=ordered:connect(s,function() calls=calls+1 end)
 ordered:emit();W.check("disconnect during emission",calls==0)
 local pending=Delegate.new(s);local added=false;local late=0
 pending:connect(s,function(self)
  if not added then added=true;pending:connect(self,function() late=late+1 end) end
 end)
 pending:emit();W.check("new subscription deferred",late==0)
 pending:emit();W.check("new subscription next emission",late==1)
 print("CHECK COMPLETE passes="..W.passes.." failures="..W.failures)
end
return M
