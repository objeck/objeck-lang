import json
 
f = open('C:\\Users\\objec\\Documents\\Temp\\Web.MsHtml.json', 'r')
data = json.load(f)
 
const = data['Constants']
first = const[0];
name = first['Name']
print(name)
f.close()