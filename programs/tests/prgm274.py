import json
 
file = open('C:\\Users\\objec\\Documents\\Temp\\Web.MsHtml.json', 'r')
data = json.load(file)
 
const = data['Constants']
first_const = const[0];
first_const_name = first_const['Name']
print(first_const_name)

file.close()