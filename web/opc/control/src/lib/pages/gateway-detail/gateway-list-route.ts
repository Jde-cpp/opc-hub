import { ListRoute } from 'jde-framework';
import { flowListRoute } from './flows/flow-list-route';
import { HistAdminKind, histAdminListRoute } from './hist-admin/hist-admin-list-route';
import { OeeAdminKind, oeeAdminListRoute } from './oee-admin/oee-admin-list-route';

//The lists a gateway's page resolves in its own tabs, each with detail pages under `<kind>/<slug>`:  the historian's (#337),
//OEE's (#404, #409) and the flows (#455).  GatewayHistList and HistAdminResolver take any by its kind.
export type GatewayListKind = HistAdminKind|OeeAdminKind|'flows';
export function gatewayListRoute( kind:GatewayListKind ):ListRoute{
	return kind=='flows' ? flowListRoute() : kind=='machines' || kind=='lines' || kind=='reasons' ? oeeAdminListRoute( kind ) : histAdminListRoute( kind );
}