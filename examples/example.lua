local yaaf = require("yaaf")

print("yaaf example")
print("endpoint: " .. yaaf.defaults.endpoint)
print("model: " .. yaaf.defaults.model)
if #yaaf.args > 0 then
    print("args: " .. table.concat(yaaf.args, ", "))
else
    print("args: none")
end