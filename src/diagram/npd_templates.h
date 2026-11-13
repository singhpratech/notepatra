// SPDX-License-Identifier: GPL-3.0-or-later
//
// The three starter templates behind the Diagram tab's New menu. Pure data in
// a header of its own so the parser tests can assert that every template
// still parses clean without linking the DiagramEditor widget.

#pragma once

namespace NpdTemplates {

// ── starter templates (every one parses clean + showcases the grammar) ──
inline constexpr const char *kFlowTemplate =
    "diagram flow\n"
    "title \"Order checkout\"\n"
    "palette auto\n"
    "direction LR\n"
    "\n"
    "# Shapes: (pill) [box] {decision} ([database]) and icon nodes.\n"
    "node start (Customer) green :: \"Signed-in shopper on the web or mobile app\"\n"
    "icon gw :gateway \"API gateway\" #1565c0 :: \"Auth, rate limiting, request routing\"\n"
    "node cart [Cart service] #1565c0 :: \"Owns the basket; idempotent on retry\"\n"
    "node stock {In stock?} orange\n"
    "icon pay :card \"Payments\" #c62828 :: \"Third-party PSP — card capture and 3-D Secure\"\n"
    "node order [Order service] #1565c0 :: \"Writes the order, emits OrderPlaced\"\n"
    "icon bus :queue \"Event bus\" #6a1b9a :: \"Kafka — at-least-once delivery\"\n"
    "icon mail :email \"Receipt mailer\" :: \"Fire-and-forget consumer\"\n"
    "node oos [Back-order] :: \"Customer is told the ETA and may wait or cancel\"\n"
    "node db ([Orders DB]) #6a1b9a :: \"Postgres — source of truth\"\n"
    "icon cache :cache \"Cache\" #6a1b9a :: \"Redis — hot order rows, 5-minute TTL\"\n"
    "\n"
    "# Groups draw a container around their members.\n"
    "group \"Edge\" : start gw\n"
    "group \"Core services\" : cart stock order\n"
    "group \"Data\" : db cache\n"
    "\n"
    "# Edges: chains, labels, a decision with two branches, a dashed async hop,\n"
    "# and a bidirectional edge.\n"
    "start <-> gw : HTTPS\n"
    "gw -> cart : add item\n"
    "cart -> stock\n"
    "stock -> order : yes\n"
    "stock -> oos : no\n"
    "order -> pay : capture\n"
    "order -> db : insert\n"
    "order -> cache : write-through\n"
    "order -.-> bus -.-> mail : receipt\n"
    "\n"
    "# Notes sit beside a node; legend explains any encoding you reuse.\n"
    "note pay \"Card capture is retried three times with exponential backoff.\"\n"
    "note bus \"Consumers must be idempotent.\"\n"
    "legend dashed \"async / best effort\"\n"
    "legend #c62828 \"third-party call\"\n"
    "\n"
    "textbox \"Solid arrows are synchronous; the mailer is fire-and-forget. Hover a node for detail.\"\n";

inline constexpr const char *kErTemplate =
    "diagram er\n"
    "title \"Orders schema\"\n"
    "palette auto\n"
    "\n"
    "node customers ([Customers]) #2e7d32 :: \"id, name, email, created_at\"\n"
    "node products ([Products]) #2e7d32 :: \"id, sku, name, unit_price\"\n"
    "node orders ([Orders]) #1565c0 :: \"id, customer_id, total, placed_at\"\n"
    "node items ([Order items]) #1565c0 :: \"id, order_id, product_id, qty, unit_price\"\n"
    "\n"
    "customers -> orders : places\n"
    "orders -> items : contains\n"
    "products -> items : referenced by\n"
    "\n"
    "textbox \"Colour groups the entities — green = master data, blue = transactional. Colour is optional.\"\n";

inline constexpr const char *kSystemTemplate =
    "diagram system\n"
    "title \"Web app architecture\"\n"
    "palette auto\n"
    "\n"
    "icon user :user \"Browser\" #00838f :: \"End-user web client\"\n"
    "icon cdn :cloud \"CDN\" #00838f :: \"Static assets + edge cache\"\n"
    "node api [API gateway] #1565c0 :: \"Auth, rate-limiting, routing\"\n"
    "icon svc :server \"App service\" #1565c0 :: \"Stateless; horizontally autoscaled\"\n"
    "icon cache :database \"Cache\" #6a1b9a :: \"Redis — sessions + hot rows\"\n"
    "icon db :database \"Primary DB\" #6a1b9a :: \"Source of truth\"\n"
    "\n"
    "user -> cdn\n"
    "user -> api\n"
    "api -> svc\n"
    "svc -> cache\n"
    "svc -> db\n"
    "cache <-> db : warm\n"
    "\n"
    "textbox \"Colour marks the tier — teal = edge, blue = app, purple = data. Colour is optional.\"\n";

}  // namespace NpdTemplates
