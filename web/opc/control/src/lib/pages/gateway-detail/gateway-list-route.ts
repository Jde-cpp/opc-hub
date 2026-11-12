import { ListRoute } from 'jde-framework';
import { HistAdminKind, histAdminListRoute } from './hist-admin/hist-admin-list-route';
import { OeeAdminKind, oeeAdminListRoute } from './oee-admin/oee-admin-list-route';

//The lists a gateway's page resolves in its own tabs, each with detail pages under `<kind>/<slug>`:  the historian's (#337) and
//OEE's (#404, #409).  GatewayHistList and HistAdminResolver take either by its kind.
export type GatewayListKind = HistAdminKind|OeeAdminKind;
export function gatewayListRoute( kind:GatewayListKind ):ListRoute{
	return kind=='machines' || kind=='lines' || kind=='reasons' ? oeeAdminListRoute( kind ) : histAdminListRoute( kind );
}