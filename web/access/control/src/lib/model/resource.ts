import { Permission, Rights } from "./permission";
import { SlugRow } from "jde-framework";

export class Resource extends SlugRow<Resource>{
	static from( resources:Partial<Resource>[] ):Resource[]{
		return resources.map( r=>new Resource(r) );
	}
	constructor(obj:any){
		super("Resource", obj);
		this.schema = obj.schema ?? obj.schemaName;
		this.criteria = obj.criteria;
		this.availableRights = obj.availableRights ?? Permission.toRights( obj.allowed );
	}

	availableRights:Rights;
	criteria:string;
	schema:string;
}
