#define OSST_SUPPORTS(SDp) (! ( memcmp (SDp->vendor, "OnStream", 8) || \
			     ( memcmp (SDp->model, "SC-", 3) && \
			       memcmp (SDp->model, "DI-", 3) && \
			       memcmp (SDp->model, "DP-", 3) && \
			       memcmp (SDp->model, "USB", 3) ) ) )
