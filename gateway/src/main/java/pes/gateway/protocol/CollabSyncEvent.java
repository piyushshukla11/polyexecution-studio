package pes.gateway.protocol;

import com.fasterxml.jackson.annotation.JsonInclude;
import java.util.Collections;
import java.util.List;

@JsonInclude(JsonInclude.Include.NON_NULL)
public class CollabSyncEvent extends ServerEvent {
    private String roomId;
    private List<String> updates;

    public CollabSyncEvent() {
        super("collab.sync");
        this.updates = Collections.emptyList();
    }

    public CollabSyncEvent(String roomId, List<String> updates) {
        super("collab.sync");
        this.roomId = roomId;
        this.updates = (updates != null) ? updates : Collections.emptyList();
    }

    public String getRoomId() {
        return roomId;
    }

    public void setRoomId(String roomId) {
        this.roomId = roomId;
    }

    public List<String> getUpdates() {
        return updates;
    }

    public void setUpdates(List<String> updates) {
        this.updates = updates;
    }
}
